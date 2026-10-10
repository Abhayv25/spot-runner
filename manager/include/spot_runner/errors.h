#pragma once

#include <stdexcept>
#include <string>

namespace spot_runner {

// Configuration file is missing, malformed, or contains invalid values.
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Transport-level HTTP failure: connect, timeout, or an unparseable response.
class HttpError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The queue no longer recognizes our claim on a job. Another manager may own
// it now, so the caller must stop work and must not write shared state.
class LeaseLostError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The container runtime (Docker or a child process) rejected an operation.
class RuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace spot_runner
