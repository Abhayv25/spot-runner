// =============================================================================
// config.cpp: load and check config/manager.local.json (PHASE 3)
// =============================================================================
// STEP 1: Includes
//   "spot_runner/config.h", <fstream>, <sstream>, <nlohmann/json.hpp>
//   using namespace std;
//   using json = nlohmann::json;
//   namespace spot_runner { ...everything below... }
//
// STEP 2: load_config(path)
//   2a. Open the file with ifstream. If it fails to open, throw runtime_error:
//         "config file not found: " + path
//   2b. Parse:  json j = json::parse(file);
//       Wrap in try/catch(json::parse_error& e) and rethrow as runtime_error with
//       e.what() included, so a typo in the JSON gives a readable message.
//   2c. Fill a Config. Required strings: use j.at("mode").get<string>()
//       (.at() throws if the key is missing; good, that's a real error).
//       Optional numbers: use j.value("poll_interval_ms", 1000) so defaults work.
//   2d. If mode == "local", fill config.local from j.at("local").
//   2e. Call validate_config(config). Return it.
//
// STEP 3: validate_config(config). Throw runtime_error for each of these:
//   - mode is not "local" or "aws"
//   - manager_id or work_dir is empty
//   - any interval/timeout <= 0
//   - stop_grace_seconds >= 110   (Amazon gives you 120s total, and you still need
//                                  time to upload after the job stops. Explain this
//                                  number in your README: it's a great detail.)
//   - lease_seconds <= checkpoint_upload_interval_seconds * 2
//         (you must renew the lease well before it runs out)
//   - mode == "local" and any local path is empty
//
// STEP 4: Test it (tests/test_config.cpp has the list).
// =============================================================================
