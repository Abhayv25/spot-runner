// =============================================================================
// prime-counter: the fake "long job" (PHASE 1 in README.md)
// =============================================================================
// What it does: counts how many prime numbers exist from 2 up to TARGET.
// It's slow on purpose, autosaves its progress, and resumes from a save file.
// It follows docs/checkpoint-contract.md exactly. Read that first.
// -----------------------------------------------------------------------------
// STEP 4: Prototypes (declare these, define them below main)
// -----------------------------------------------------------------------------
//   void handle_signal(int signal_number);
//   string get_env_or(const string& name, const string& fallback);
//   bool is_prime(int64_t n);
//   bool load_checkpoint(const string& path, Progress& progress);
//   bool save_checkpoint_atomically(const string& path, const Progress& progress);
//   bool write_result(const string& path, const Progress& progress);
//
// -----------------------------------------------------------------------------
// STEP 5: main()
// -----------------------------------------------------------------------------
//   5a. Install the signal handler for BOTH SIGTERM and SIGINT:
//         signal(SIGTERM, handle_signal); signal(SIGINT, handle_signal);
//
//   5b. Read settings with get_env_or:
//         JOB_ID           (fallback "local-test")
//         CHECKPOINT_DIR   (fallback "./checkpoint" so it works outside Docker)
//         TARGET           (fallback "100000000"), convert with stoll
//         AUTOSAVE_SECONDS (fallback "10"), convert with stoi
//       Build two paths: CHECKPOINT_DIR + "/checkpoint.dat" and "/result.dat".
//       Create CHECKPOINT_DIR if it doesn't exist (filesystem::create_directories).
//
//   5c. Make a Progress. Call load_checkpoint. Print ONE line either way:
//         "[job-001] resuming from 48,000,001 (2,891,000 primes so far)"
//         "[job-001] no checkpoint, starting fresh"
//       (cout, then flush with endl. Docker logs only show flushed output.)
//
//   5d. Remember the time of the last autosave (chrono::steady_clock::now()).
//
//   5e. The main loop: while next_number <= TARGET
//         - If g_stop_requested is true:
//             save_checkpoint_atomically, print "[job] stop requested, saved at N",
//             return 75.   (75 = "stopped early, safe to resume")
//         - If is_prime(next_number): increase primes_found, set last_prime.
//         - Increase next_number by 1.
//         - Every 100,000 numbers (use %), check if AUTOSAVE_SECONDS have passed
//           since the last autosave. If yes: save, print a short progress line,
//           reset the timer. (Checking the clock every single number is slow.)
//
//   5f. After the loop (finished!):
//         write_result, then save one last checkpoint too, print a summary,
//         return 0.
//
//   5g. If any save/write fails, print the error to cerr and return 1.
//
// -----------------------------------------------------------------------------
// STEP 6: Define the helper functions (below main)
// -----------------------------------------------------------------------------
//   handle_signal:
//     Only set g_stop_requested = true. Do NOT print or save inside a signal
//     handler. Most functions aren't safe to call from one. The main loop
//     notices the flag and does the real work.
//
//   get_env_or:
//     Call getenv(name.c_str()). If it returns nullptr, return fallback.
//
//   is_prime:
//     n < 2 -> false. 2 -> true. Even -> false.
//     Then try odd divisors i from 3 while i * i <= n. If n % i == 0 -> false.
//     Return true. (Simple trial division is fine. Slow is the point.)
//
//   load_checkpoint:
//     If the file doesn't exist, return false.
//     The format is up to you (the manager never reads it). Simple choice:
//     three lines of "key=value":
//         next_number=48000001
//         primes_found=2891000
//         last_prime=47999987
//     Read each line, split on '=', fill the struct with stoll.
//     If anything looks wrong (missing key, bad number), print a warning and
//     return false so the job starts fresh instead of crashing.
//
//   save_checkpoint_atomically:  (THE most important function in this file)
//     1. Open path + ".tmp" with ofstream (truncate mode).
//     2. Write the three key=value lines.
//     3. Call flush() and check the stream is still good.
//     4. Close it.
//     5. filesystem::rename(tmp_path, path). Rename replaces the old file in
//        one step, so a reader sees either the old save or the new one, never
//        half of one. Wrap in try/catch(filesystem::filesystem_error&).
//     Return true on success.
//     (Bonus later: real fsync needs <fcntl.h>/<unistd.h> and a file
//      descriptor. Mention it in your README as a known trade-off.)
//
//   write_result:
//     Same atomic tmp-then-rename steps, writing a short summary:
//         target=...  primes_found=...  last_prime=...
//
// -----------------------------------------------------------------------------
// STEP 7: Test it WITHOUT Docker (do all of these before Phase 2)
// -----------------------------------------------------------------------------
//   Build:   cmake --preset debug && cmake --build --preset debug
//   Run:     ./build/debug/jobs/prime-counter/prime-counter
//   Test A:  let it finish with a small TARGET (TARGET=100000 ./prime-counter).
//            Exit code should be 0 (check with: echo $?). result.dat exists.
//   Test B:  big TARGET, press Ctrl+C after a few autosaves. Exit code 75.
//            Run again: it must print "resuming from ...".
//   Test C:  run it, then from another terminal: kill -9 <pid> (can't be caught).
//            Run again: it resumes from the last AUTOSAVE, not from 2.
//   Test D:  final count must be the same whether or not you interrupted it.
//            (Primes up to 1,000,000 = 78,498. Use that to check correctness.)
// =============================================================================

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace std;

struct Progress {
    int64_t next_number = 2;
    int64_t primes_found = 0;
    int64_t last_prime = 0;
};

atomic<bool> g_stop_requested{false};

void handle_signal(int signal_number);
string get_env_or(const string& name, const string& fallback);
bool is_prime(int64_t n);
bool load_checkpoint(const string& path, Progress& progress);
bool save_checkpoint_atomically(const string& path, const Progress& progress);
bool write_result(const string& path, const Progress& progress);

string get_env_or(const string& name, const string& fallback) {
    const char* value = getenv(name.c_str());
    if (value == nullptr) {
        return fallback;
    }

    return value;
};

bool is_prime(int64_t n) {
    if (n < 2) {
        return false;
    } else if (n == 2) {
        return true;
    } else if (n % 2 == 0) {
        return false;
    }

    for (int64_t i = 3; i * i <= n; i += 2) {
        if (i % n == 0) {
            return false;
        }
        return true;
    }
}
