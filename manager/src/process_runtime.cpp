#include "spot_runner/process_runtime.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

#include "spot_runner/errors.h"
#include "spot_runner/logger.h"
#include "spot_runner/util.h"

namespace spot_runner {

namespace {

int decode_wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);  // same convention as shells and Docker
    }
    return -1;
}

}  // namespace

ProcessRuntime::ProcessRuntime(std::string log_dir) : log_dir_(std::move(log_dir)) {
    std::filesystem::create_directories(log_dir_);
}

ProcessRuntime::~ProcessRuntime() {
    for (auto& [handle, child] : children_) {
        poll_exit(child);
        if (!child.exited) {
            ::kill(child.pid, SIGKILL);
            int status = 0;
            ::waitpid(child.pid, &status, 0);
        }
    }
}

void ProcessRuntime::ensure_image(const std::string& image) {
    if (::access(image.c_str(), X_OK) != 0) {
        throw RuntimeError("job executable not found or not executable: " + image);
    }
}

std::string ProcessRuntime::start(const ContainerSpec& spec) {
    ensure_image(spec.image);

    // Everything the child needs is prepared before fork(). After fork() in a
    // multithreaded process the child may only call async-signal-safe
    // functions, so it cannot allocate, format strings, or take locks.
    std::vector<std::string> env_storage;
    for (const auto& [key, value] : spec.env) {
        env_storage.push_back(key + "=" + value);
    }
    env_storage.push_back("CHECKPOINT_DIR=" + spec.checkpoint_dir);
    if (const auto path = getenv_string("PATH")) {
        env_storage.push_back("PATH=" + *path);
    }
    std::vector<char*> envp;
    for (auto& entry : env_storage) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    // Resolve now: the child changes into the checkpoint directory before exec.
    std::string program = std::filesystem::absolute(spec.image).string();
    char* argv[] = {program.data(), nullptr};

    const std::string log_path = (std::filesystem::path(log_dir_) / (spec.name + ".log")).string();
    const int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0) {
        throw RuntimeError("open " + log_path + ": " + std::strerror(errno));
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        ::close(log_fd);
        throw RuntimeError(std::string("fork: ") + std::strerror(saved));
    }

    if (pid == 0) {
        // Child. The manager blocks SIGINT/SIGTERM in all threads and handles
        // them with sigwait(); the mask is inherited across exec, so unblock
        // them or the job would never see its stop signal.
        sigset_t all;
        sigemptyset(&all);
        ::sigprocmask(SIG_SETMASK, &all, nullptr);
        ::signal(SIGPIPE, SIG_DFL);

        ::dup2(log_fd, STDOUT_FILENO);
        ::dup2(log_fd, STDERR_FILENO);
        const int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
        }
        if (::chdir(spec.checkpoint_dir.c_str()) != 0) {
            // Not fatal: the job finds its directory through CHECKPOINT_DIR.
        }
        ::execve(program.c_str(), argv, envp.data());
        ::_exit(127);  // exec failed
    }

    ::close(log_fd);
    const std::string handle = std::to_string(pid);
    children_[handle] = Child{pid, log_path, false, 0};
    log::debug("started process " + spec.name + " (pid " + handle + ")");
    return handle;
}

void ProcessRuntime::poll_exit(Child& child) {
    if (child.exited) {
        return;
    }
    int status = 0;
    const pid_t rc = ::waitpid(child.pid, &status, WNOHANG);
    if (rc == child.pid) {
        child.exited = true;
        child.exit_code = decode_wait_status(status);
    } else if (rc < 0 && errno == ECHILD) {
        child.exited = true;  // already reaped elsewhere; exit code unknown
        child.exit_code = -1;
    }
}

ProcessRuntime::Child& ProcessRuntime::find(const std::string& handle) {
    const auto it = children_.find(handle);
    if (it == children_.end()) {
        throw RuntimeError("unknown process handle " + handle);
    }
    return it->second;
}

ContainerStatus ProcessRuntime::inspect(const std::string& handle) {
    const auto it = children_.find(handle);
    if (it == children_.end()) {
        return ContainerStatus{};
    }
    poll_exit(it->second);
    return ContainerStatus{true, !it->second.exited, it->second.exit_code};
}

void ProcessRuntime::stop(const std::string& handle, std::chrono::seconds grace) {
    Child& child = find(handle);
    poll_exit(child);
    if (child.exited) {
        return;
    }
    ::kill(child.pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + grace;
    while (std::chrono::steady_clock::now() < deadline) {
        poll_exit(child);
        if (child.exited) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    log::warn("process " + handle + " ignored SIGTERM for " + std::to_string(grace.count()) +
              "s, sending SIGKILL");
    ::kill(child.pid, SIGKILL);
    int status = 0;
    ::waitpid(child.pid, &status, 0);
    child.exited = true;
    child.exit_code = decode_wait_status(status);
}

void ProcessRuntime::remove(const std::string& handle) {
    const auto it = children_.find(handle);
    if (it == children_.end()) {
        return;
    }
    poll_exit(it->second);
    if (!it->second.exited) {
        ::kill(it->second.pid, SIGKILL);
        int status = 0;
        ::waitpid(it->second.pid, &status, 0);
    }
    children_.erase(it);
}

std::string ProcessRuntime::logs_tail(const std::string& handle, int lines) {
    const auto it = children_.find(handle);
    if (it == children_.end()) {
        return "";
    }
    try {
        return tail_lines(read_file(it->second.log_path), static_cast<std::size_t>(lines));
    } catch (const std::exception&) {
        return "";
    }
}

}  // namespace spot_runner
