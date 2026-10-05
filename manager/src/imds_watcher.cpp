// =============================================================================
// imds_watcher.cpp: real spot interruption warning from EC2 (PHASE 7)
// =============================================================================
// How Amazon tells a spot computer it's being taken back:
//   GET http://169.254.169.254/latest/meta-data/spot/instance-action
//     404               -> nothing is happening (the normal case)
//     200 + JSON body   -> {"action": "terminate", "time": "2026-10-04T18:05:00Z"}
//
// IMDSv2 (the secure version, required on most new instances) needs a token first:
//   PUT /latest/api/token
//       header  X-aws-ec2-metadata-token-ttl-seconds: 21600   (token lasts 6 hours)
//       -> body is the token text
//   Then send header  X-aws-ec2-metadata-token: <token>  on every GET.
//
// STEP 1: Includes: "spot_runner/imds_watcher.h", "spot_runner/logger.h",
//   <nlohmann/json.hpp>.  using namespace std; namespace spot_runner { ... }
//
// STEP 2: Constructors
//   Default: endpoint_ = tcp_endpoint("169.254.169.254", 80)
//   Other: store the given endpoint (tests can point at a fake server).
//
// STEP 3: get_token()
//   If token_ isn't empty and now < token_expires_, return token_ (reuse it).
//   Otherwise send the PUT above with a SHORT timeout (1000 ms). Save the token and
//   set token_expires_ to now + 6 hours minus a safety margin (say 5 minutes).
//
// STEP 4: check()
//   Everything inside try { } catch (exception& e) { log_warn(...); return nullopt; }
//   (A temporary network blip must never crash the manager.)
//   4a. GET /latest/meta-data/spot/instance-action with the token header, 1000 ms timeout.
//   4b. 404 -> return nullopt.
//   4c. 401 -> the token expired: clear token_ so the next check gets a new one.
//   4d. 200 -> parse JSON, return InterruptionNotice{action, time}.
//
// BONUS (great README talking point): Amazon also sends an EARLIER, softer hint:
//   GET /latest/meta-data/events/recommendations/rebalance
//   200 means "this computer is at higher risk of being taken soon". On that hint you
//   could stop taking NEW jobs while letting the current one keep running.
//
// You can only truly test this on EC2. Test it in Phase 8 with AWS's
// Fault Injection Service, which can send a real interruption to your instance.
// =============================================================================
