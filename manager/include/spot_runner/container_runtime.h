// =============================================================================
// container_runtime.h: the INTERFACE for "something that runs containers" (PHASE 5)
// =============================================================================
// An interface is a class with only pure virtual functions. It describes WHAT can
// be done, not HOW. DockerClient is the real "how". In tests, a FakeRuntime is a
// pretend "how", so you can test JobRunner without Docker running.
// This exact pattern (interface + real + fake) repeats for storage, queue and watcher.
// Interviewers call it "dependency injection". Be ready to explain why you used it.
//
// STEP 1: #pragma once, includes <string>, <map>, <optional>, namespace spot_runner
//
// STEP 2: struct ContainerSpec (everything needed to start one container)
//   std::string image;                           // "prime-counter:latest"
//   std::string name;                            // e.g. "spot-job-001", must be unique
//   std::map<std::string, std::string> env;      // JOB_ID, CHECKPOINT_DIR, TARGET...
//   std::string host_checkpoint_dir;             // ABSOLUTE path on the computer
//   std::string container_checkpoint_dir = "/checkpoint";
//
// STEP 3: struct ContainerStatus
//   bool running = false;
//   bool exists = false;                         // false if it was removed
//   int exit_code = 0;                           // only meaningful when !running
//
// STEP 4: class ContainerRuntime (the interface)
//   public:
//     virtual ~ContainerRuntime() = default;     // ALWAYS a virtual destructor in a base class
//     virtual bool ping() = 0;                   // "is Docker there?"
//     virtual void ensure_image(const std::string& image) = 0;   // pull if missing
//     virtual std::string create_and_start(const ContainerSpec& spec) = 0; // returns id
//     virtual ContainerStatus inspect(const std::string& container_id) = 0;
//     virtual void stop(const std::string& container_id, int grace_seconds) = 0;
//     virtual void remove(const std::string& container_id) = 0;
//     virtual std::string logs_tail(const std::string& container_id, int lines) = 0;
//   (No .cpp for this file. Interfaces don't need one.)
// =============================================================================
