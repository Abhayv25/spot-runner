#!/usr/bin/env python3
"""Fault-injection test for spot-runner.

Runs several manager processes against a shared local queue and storage
directory (stand-ins for SQS and S3), then repeatedly does one of two things to
a random manager:

  graceful  create its interruption trigger file, which goes through the same
            code path as an EC2 two-minute spot interruption notice
  hard      SIGKILL the manager's process group and its job container with no
            warning, which is what an instance that vanishes looks like

Killed or interrupted managers are replaced with fresh ones (new IDs), the way
an Auto Scaling group replaces reclaimed instances. When every job has a result,
the script verifies each one against an independent sieve of Eratosthenes and
computes recovery metrics from the managers' structured event logs.

Example:
  python3 scripts/chaos_test.py --manager build/release/manager/spot-runner \
      --runtime docker --image prime-counter:latest --managers 3 --jobs 30

Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
import json
import os
import random
import shutil
import signal
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

@dataclass
class Manager:
    manager_id: str
    process: subprocess.Popen
    started_at: float


@dataclass
class ChaosAction:
    kind: str  # "graceful" or "hard"
    manager_id: str
    ts_ms: int


@dataclass
class Run:
    root: Path
    args: argparse.Namespace
    managers: dict[str, Manager] = field(default_factory=dict)
    actions: list[ChaosAction] = field(default_factory=list)
    next_index: int = 0


def now_ms() -> int:
    return int(time.time() * 1000)


def write_config(run: Run, manager_id: str) -> Path:
    args = run.args
    config = {
        "backend": "local",
        "runtime": args.runtime,
        "manager_id": manager_id,
        "work_dir": str(run.root / "work" / manager_id),
        "event_log": str(run.root / "events" / f"{manager_id}.jsonl"),
        "max_attempts": 5,
        "timing": {
            "poll_interval_ms": 200,
            "checkpoint_sync_interval_ms": args.sync_ms,
            "lease_duration_s": args.lease_s,
            "stop_grace_period_s": 10,
            "idle_backoff_ms": 300,
        },
        "local": {
            "storage_dir": str(run.root / "storage"),
            "queue_dir": str(run.root / "queue"),
            "interrupt_file": str(run.root / "triggers" / manager_id),
        },
    }
    path = run.root / "configs" / f"{manager_id}.json"
    path.write_text(json.dumps(config, indent=2))
    return path


def start_manager(run: Run) -> Manager:
    manager_id = f"m{run.next_index:03d}"
    run.next_index += 1
    config = write_config(run, manager_id)
    log = open(run.root / "logs" / f"{manager_id}.log", "w")
    # start_new_session puts the manager (and, with the process runtime, its job)
    # in its own process group, so a hard kill can take down the whole "machine".
    process = subprocess.Popen(
        [run.args.manager, "run", "--config", str(config)],
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    manager = Manager(manager_id, process, time.time())
    run.managers[manager_id] = manager
    return manager


def enqueue_jobs(run: Run, targets: list[int]) -> None:
    config = write_config(run, "enqueuer")
    files = []
    for i, target in enumerate(targets):
        job = {
            "job_id": f"job-{i:03d}",
            "image": run.args.image,
            "env": {"TARGET": str(target), "AUTOSAVE_MS": str(run.args.autosave_ms)},
        }
        path = run.root / "jobs" / f"job-{i:03d}.json"
        path.write_text(json.dumps(job))
        files.append(str(path))
    subprocess.run([run.args.manager, "enqueue", "--config", str(config), *files],
                   check=True, stdout=subprocess.DEVNULL)


def hard_kill(run: Run, manager: Manager) -> None:
    try:
        os.killpg(manager.process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    manager.process.wait()
    if run.args.runtime == "docker":
        # A real instance loss takes its containers with it.
        ids = subprocess.run(
            ["docker", "ps", "-q", "--filter", f"label=spot-runner.manager={manager.manager_id}"],
            capture_output=True, text=True).stdout.split()
        if ids:
            subprocess.run(["docker", "kill", *ids], capture_output=True)


# ---------------------------------------------------------------------------
# Verification and metrics
# ---------------------------------------------------------------------------

def prime_counts(targets: list[int]) -> dict[int, int]:
    limit = max(targets)
    sieve = bytearray([1]) * (limit + 1)
    sieve[0:2] = b"\x00\x00"
    for p in range(2, int(limit ** 0.5) + 1):
        if sieve[p]:
            sieve[p * p::p] = bytearray(len(range(p * p, limit + 1, p)))
    counts = {}
    for target in set(targets):
        counts[target] = sum(sieve[: target + 1])
    return counts


def load_events(root: Path) -> list[dict]:
    events = []
    for path in (root / "events").glob("*.jsonl"):
        for line in path.read_text().splitlines():
            if line.strip():
                events.append(json.loads(line))
    events.sort(key=lambda e: e["ts"])
    return events


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(pct / 100 * (len(ordered) - 1))))
    return ordered[index]


def summarize(name: str, values_ms: list[float]) -> dict:
    if not values_ms:
        return {"name": name, "count": 0}
    seconds = [v / 1000 for v in values_ms]
    return {
        "name": name,
        "count": len(seconds),
        "p50_s": round(statistics.median(seconds), 2),
        "p95_s": round(percentile(seconds, 95), 2),
        "max_s": round(max(seconds), 2),
        "mean_s": round(statistics.mean(seconds), 2),
    }


def analyze(run: Run, targets: list[int], wall_seconds: float) -> dict:
    events = load_events(run.root)
    terminal = {"job_completed", "job_released", "job_failed", "job_dead_lettered", "lease_lost"}

    # Attempt intervals: (manager, job) from job_claimed until a terminal event,
    # or until the manager was hard-killed.
    kills = {a.manager_id: a.ts_ms for a in run.actions if a.kind == "hard"}
    open_attempts: dict[tuple[str, str], dict] = {}
    intervals: list[dict] = []
    for e in events:
        key = (e["manager"], e.get("job", ""))
        if e["event"] == "job_claimed":
            open_attempts[key] = {"manager": e["manager"], "job": e["job"], "start": e["ts"],
                                  "end": None, "last_checkpoint": None}
        elif e["event"] == "checkpoint_uploaded" and key in open_attempts:
            open_attempts[key]["last_checkpoint"] = e["ts"]
        elif e["event"] in terminal and key in open_attempts:
            attempt = open_attempts.pop(key)
            attempt["end"] = e["ts"]
            attempt["outcome"] = e["event"]
            intervals.append(attempt)
    for key, attempt in open_attempts.items():
        kill_ts = kills.get(attempt["manager"])
        attempt["end"] = kill_ts
        attempt["outcome"] = "hard_killed" if kill_ts else "unterminated"
        intervals.append(attempt)

    # 1. No job may be owned by two managers at the same time.
    overlaps = 0
    by_job: dict[str, list[dict]] = {}
    for attempt in intervals:
        by_job.setdefault(attempt["job"], []).append(attempt)
    for attempts in by_job.values():
        attempts.sort(key=lambda a: a["start"])
        for a, b in zip(attempts, attempts[1:]):
            if a["end"] is None or b["start"] < a["end"]:
                overlaps += 1

    claims_by_job: dict[str, list[int]] = {}
    for e in events:
        if e["event"] == "job_claimed":
            claims_by_job.setdefault(e["job"], []).append(e["ts"])

    def next_claim(job: str, after: int) -> int | None:
        later = [ts for ts in claims_by_job.get(job, []) if ts > after]
        return min(later) if later else None

    # 2. Graceful interruptions: notice -> checkpoint uploaded and job released,
    #    then release -> resumed by another manager.
    notice_ts = {e["manager"]: e["ts"] for e in events if e["event"] == "interruption_notice"}
    handoff_ms, graceful_resume_ms = [], []
    for e in events:
        if e["event"] == "job_released" and e["manager"] in notice_ts:
            handoff_ms.append(e["ts"] - notice_ts[e["manager"]])
            resumed = next_claim(e["job"], e["ts"])
            if resumed:
                graceful_resume_ms.append(resumed - e["ts"])

    # 3. Hard kills: kill -> job claimed again (bounded by the lease), and the
    #    work lost = time since the last checkpoint that reached storage.
    hard_recovery_ms, lost_work_ms = [], []
    for attempt in intervals:
        if attempt["outcome"] != "hard_killed":
            continue
        kill_ts = attempt["end"]
        resumed = next_claim(attempt["job"], kill_ts)
        if resumed:
            hard_recovery_ms.append(resumed - kill_ts)
        baseline = attempt["last_checkpoint"] or attempt["start"]
        lost_work_ms.append(kill_ts - baseline)

    # 4. Every job's result must match an independent sieve.
    expected = prime_counts(targets)
    correct, wrong, missing = 0, [], []
    for i, target in enumerate(targets):
        result = run.root / "storage" / "jobs" / f"job-{i:03d}" / "result.dat"
        if not result.exists():
            missing.append(f"job-{i:03d}")
            continue
        fields = dict(line.split("=", 1) for line in result.read_text().split())
        if int(fields["primes_found"]) == expected[target]:
            correct += 1
        else:
            wrong.append(f"job-{i:03d}")

    dead = list((run.root / "queue" / "dead").glob("*.json"))
    count = lambda name: sum(1 for e in events if e["event"] == name)  # noqa: E731
    return {
        "config": {
            "managers": run.args.managers, "jobs": len(targets), "runtime": run.args.runtime,
            "seed": run.args.seed, "lease_s": run.args.lease_s, "sync_ms": run.args.sync_ms,
            "autosave_ms": run.args.autosave_ms,
            "target_range": [min(targets), max(targets)],
        },
        "wall_clock_s": round(wall_seconds, 1),
        "faults_injected": {
            "graceful_interruptions": sum(1 for a in run.actions if a.kind == "graceful"),
            "hard_kills": sum(1 for a in run.actions if a.kind == "hard"),
            "jobs_hard_killed_mid_run": sum(1 for a in intervals if a["outcome"] == "hard_killed"),
        },
        "correctness": {
            "jobs_correct": correct, "jobs_wrong": wrong, "jobs_missing": missing,
            "dead_lettered": len(dead), "concurrent_ownership_violations": overlaps,
        },
        "activity": {
            "attempts_started": count("job_started"),
            "checkpoints_uploaded": count("checkpoint_uploaded"),
            "jobs_resumed_from_checkpoint": sum(
                1 for e in events if e["event"] == "job_claimed" and e.get("resumed")),
        },
        "graceful_handoff": summarize("notice -> checkpoint saved and job released", handoff_ms),
        "graceful_resume": summarize("released -> resumed on another manager", graceful_resume_ms),
        "hard_kill_recovery": summarize("kill -> resumed on another manager", hard_recovery_ms),
        "work_lost_per_hard_kill": summarize("kill - last durable checkpoint", lost_work_ms),
    }


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manager", required=True, help="path to the spot-runner binary")
    parser.add_argument("--runtime", choices=["docker", "process"], default="docker")
    parser.add_argument("--image", required=True,
                        help="Docker image, or the prime-counter path for --runtime process")
    parser.add_argument("--managers", type=int, default=3)
    parser.add_argument("--jobs", type=int, default=30)
    parser.add_argument("--min-target", type=int, default=15_000_000)
    parser.add_argument("--max-target", type=int, default=45_000_000)
    parser.add_argument("--fault-interval", type=float, nargs=2, default=[4.0, 8.0],
                        metavar=("MIN_S", "MAX_S"), help="seconds between injected faults")
    parser.add_argument("--hard-fraction", type=float, default=0.5,
                        help="fraction of faults that are hard kills")
    parser.add_argument("--lease-s", type=int, default=6)
    parser.add_argument("--sync-ms", type=int, default=1000)
    parser.add_argument("--autosave-ms", type=int, default=1000)
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--root", default="run/chaos")
    parser.add_argument("--report", default=None, help="write the JSON report here")
    args = parser.parse_args()

    random.seed(args.seed)
    root = Path(args.root).resolve()
    shutil.rmtree(root, ignore_errors=True)
    for sub in ("configs", "events", "jobs", "logs", "triggers", "storage", "queue", "work"):
        (root / sub).mkdir(parents=True)
    run = Run(root=root, args=args)

    targets = [random.randint(args.min_target, args.max_target) for _ in range(args.jobs)]
    enqueue_jobs(run, targets)
    for _ in range(args.managers):
        start_manager(run)

    started = time.time()
    next_fault = started + random.uniform(*args.fault_interval)
    results_dir = root / "storage" / "jobs"

    def finished() -> int:
        return sum(1 for i in range(args.jobs) if (results_dir / f"job-{i:03d}" / "result.dat").exists())

    try:
        while finished() + len(list((root / "queue" / "dead").glob("*.json"))) < args.jobs:
            if time.time() - started > args.timeout:
                print("timed out", file=sys.stderr)
                break

            # Replace managers that exited (after a graceful interruption).
            for manager_id, manager in list(run.managers.items()):
                if manager.process.poll() is not None:
                    del run.managers[manager_id]
                    start_manager(run)

            if time.time() >= next_fault and run.managers:
                victim = random.choice(list(run.managers.values()))
                kind = "hard" if random.random() < args.hard_fraction else "graceful"
                run.actions.append(ChaosAction(kind, victim.manager_id, now_ms()))
                if kind == "hard":
                    hard_kill(run, victim)
                    del run.managers[victim.manager_id]
                    start_manager(run)
                else:
                    (root / "triggers" / victim.manager_id).write_text("")
                print(f"[{time.time() - started:6.1f}s] {kind:8s} {victim.manager_id}  "
                      f"({finished()}/{args.jobs} done)", flush=True)
                next_fault = time.time() + random.uniform(*args.fault_interval)

            time.sleep(0.2)
    finally:
        for manager in run.managers.values():
            if manager.process.poll() is None:
                manager.process.send_signal(signal.SIGTERM)
        for manager in run.managers.values():
            try:
                manager.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(manager.process.pid, signal.SIGKILL)

    report = analyze(run, targets, time.time() - started)
    text = json.dumps(report, indent=2)
    print(text)
    if args.report:
        Path(args.report).write_text(text + "\n")

    c = report["correctness"]
    ok = (c["jobs_correct"] == args.jobs and not c["jobs_wrong"] and not c["jobs_missing"]
          and c["concurrent_ownership_violations"] == 0)
    print("PASS" if ok else "FAIL", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
