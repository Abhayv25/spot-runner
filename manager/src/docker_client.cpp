// =============================================================================
// docker_client.cpp: control Docker through its HTTP API (PHASE 5)
// =============================================================================
// Docker Desktop must be running. Watch what you're doing in the Docker Desktop
// app or with "docker ps -a" in a terminal while you test each function.
//
// STEP 1: Includes
//   "spot_runner/docker_client.h", "spot_runner/logger.h", <nlohmann/json.hpp>
//   using namespace std; using json = nlohmann::json; namespace spot_runner { ... }
//
// STEP 2: Constructor: endpoint_ = unix_endpoint(socket_path)
//
// STEP 3: call(method, path, json_body)
//   Build an HttpRequest. If json_body isn't empty, add header
//   "Content-Type: application/json". send_request(endpoint_, request, timeout_ms_).
//   If status_code >= 400: throw runtime_error with the method, path, status code
//   AND the body (Docker explains errors in the body as {"message": "..."}).
//   Return the response.
//
//   Path tip: Docker accepts paths with no version ("/containers/json"). Later you
//   can pin a version ("/v1.43/containers/json"); GET /version shows yours.
//
// STEP 4: ping()
//   GET /_ping. Return true if status 200 and body "OK". Catch exceptions -> false.
//
// STEP 5: ensure_image(image)
//   5a. GET /images/<image>/json. 200 = already here, return. 404 = need to pull.
//       (Call send_request directly here, since call() would throw on 404.)
//   5b. Pull: POST /images/create?fromImage=<name>&tag=<tag>
//       Split "prime-counter:latest" at the LAST ':' into name and tag.
//       The reply streams progress lines; you can ignore them, but check the
//       final line has no "error" field.
//   For local images you built with "docker build", 5a always finds them, so the
//   pull path only matters on EC2. (Pulling from Amazon's private registry needs an
//   "X-Registry-Auth" header. That's Phase 8; note it and move on.)
//
// STEP 6: create_and_start(spec)
//   6a. Build the JSON body with nlohmann::json:
//         {
//           "Image": spec.image,
//           "Env": ["JOB_ID=job-001", "CHECKPOINT_DIR=/checkpoint", "TARGET=..."],
//           "HostConfig": {
//             "Binds": ["<spec.host_checkpoint_dir>:/checkpoint"]
//           },
//           "StopSignal": "SIGTERM"
//         }
//       "Env" is a list of "KEY=VALUE" strings: loop over spec.env to build it.
//       "Binds" is the SHARED FOLDER. The host side must be an ABSOLUTE path.
//   6b. If a container with this name already exists (left over from a crash),
//       remove it first: DELETE /containers/<name>?force=true and ignore a 404.
//   6c. POST /containers/create?name=<spec.name> with the body. Reply has {"Id": "..."}.
//   6d. POST /containers/<id>/start (no body). Expect 204.
//   6e. Log "started <name> (<first 12 chars of id>)". Return the id.
//
// STEP 7: inspect(id)
//   GET /containers/<id>/json. 404 -> return {exists=false}.
//   Otherwise read  State.Running  and  State.ExitCode  from the JSON.
//
// STEP 8: stop(id, grace_seconds)
//   POST /containers/<id>/stop?t=<grace_seconds>
//   Docker sends SIGTERM, waits up to t seconds, then SIGKILL. This call BLOCKS until
//   the container is stopped, so set timeout_ms_ high enough
//   ((grace_seconds + 10) * 1000) for this call. 304 = already stopped; that's fine.
//
// STEP 9: remove(id)
//   DELETE /containers/<id>?force=true. Ignore 404.
//
// STEP 10: logs_tail(id, lines)  (for debugging: show the job's last output)
//   GET /containers/<id>/logs?stdout=1&stderr=1&tail=<lines>
//   Gotcha: the body is "multiplexed". Each chunk starts with an 8-byte header:
//     byte 0 = stream (1 stdout, 2 stderr), bytes 4-7 = length (big-endian).
//   Loop: read 8 bytes, read <length> bytes of text, repeat. Return the text joined.
//
// STEP 11: Test by hand with a tiny scratch main (or the manager's --check-docker
//   flag from main.cpp): ping -> ensure_image -> create_and_start -> wait ->
//   inspect -> stop -> inspect (exit code should be 75!) -> remove.
// =============================================================================
