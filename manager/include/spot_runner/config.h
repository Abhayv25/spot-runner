// =============================================================================
// config.h: the manager's settings (PHASE 3)
// =============================================================================
// Holds everything from config/manager.local.json in one struct, so the rest of
// the program never touches JSON directly.
//
// Header rules for this whole project:
//   - Start every header with:  #pragma once   (stops it being included twice)
//   - Do NOT put "using namespace std;" in a header. It leaks into every file that
//     includes it. Write std::string here; use "using namespace std;" in .cpp files.
//   - Wrap everything in:  namespace spot_runner { ... }
//   - Headers hold prototypes (declarations). Definitions go in the matching .cpp.
//
// STEP 1: Includes: <string>, <stdexcept>
//
// STEP 2: struct LocalSettings
//   Three std::string fields matching the "local" block of the JSON:
//     storage_dir, queue_dir, interrupt_file
//
// STEP 3: struct Config
//   Fields (match the JSON names exactly so it's easy to follow):
//     std::string mode;                         // "local" now, "aws" in Phase 7
//     std::string manager_id;
//     std::string work_dir;
//     std::string docker_socket;
//     int poll_interval_ms;
//     int checkpoint_upload_interval_seconds;
//     int stop_grace_seconds;
//     int lease_seconds;
//     int max_attempts;
//     int idle_wait_seconds;
//     LocalSettings local;
//   Give every int a sensible default value right in the struct (e.g. = 1000).
//
//   Phase 7: add a struct AwsSettings { region, bucket, queue_url } and an
//   "aws" field here. Leave it out until then.
//
// STEP 4: Prototypes (free functions, not inside the struct)
//   Config load_config(const std::string& path);
//       Reads and checks the file. Throws std::runtime_error with a clear
//       message if the file is missing or a setting is invalid.
//   void validate_config(const Config& config);
//       Throws std::runtime_error if something makes no sense (see config.cpp).
// =============================================================================
