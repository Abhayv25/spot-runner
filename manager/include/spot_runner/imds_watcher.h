// =============================================================================
// imds_watcher.h: the REAL interruption warning on EC2 (PHASE 7)
// =============================================================================
// IMDS = Instance Metadata Service: a little web server every EC2 computer can
// reach at http://169.254.169.254. It only exists inside AWS, so this class can't
// do anything on your Mac (that's why FileWatcher exists).
// Uses your own http_client (plain TCP). No AWS SDK needed, so it always compiles.
//
// STEP 1: #pragma once
//   includes "spot_runner/interruption_watcher.h", "spot_runner/http_client.h", <chrono>
//   namespace spot_runner
//
// STEP 2: class ImdsWatcher : public InterruptionWatcher
//   public:
//     ImdsWatcher();                       // default endpoint 169.254.169.254:80
//     explicit ImdsWatcher(const Endpoint& endpoint);  // lets tests point it elsewhere
//     std::optional<InterruptionNotice> check() override;
//   private:
//     std::string get_token();             // IMDSv2 needs a session token (see .cpp)
//     Endpoint endpoint_;
//     std::string token_;
//     std::chrono::steady_clock::time_point token_expires_;
// =============================================================================
