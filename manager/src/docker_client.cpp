#include "spot_runner/docker_client.h"

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "spot_runner/errors.h"
#include "spot_runner/logger.h"
#include "spot_runner/util.h"

namespace spot_runner {

namespace {

using nlohmann::json;

constexpr const char* kManagerLabel = "spot-runner.manager";

// "registry.example.com:5000/team/app:v1" -> ("registry.example.com:5000/team/app", "v1")
std::pair<std::string, std::string> split_image(const std::string& image) {
    const auto at = image.find('@');
    if (at != std::string::npos) {
        return {image.substr(0, at), image.substr(at + 1)};  // digest reference
    }
    const auto colon = image.rfind(':');
    const auto slash = image.rfind('/');
    if (colon != std::string::npos && (slash == std::string::npos || colon > slash)) {
        return {image.substr(0, colon), image.substr(colon + 1)};
    }
    return {image, "latest"};
}

std::string describe(const HttpResponse& response) {
    try {
        const auto body = json::parse(response.body);
        if (body.contains("message")) {
            return std::to_string(response.status) + ": " + body["message"].get<std::string>();
        }
    } catch (const json::exception&) {
    }
    return std::to_string(response.status) + ": " + response.body;
}

}  // namespace

DockerClient::DockerClient(std::string socket_path, std::string registry_auth_file)
    : endpoint_(Endpoint::unix_socket(std::move(socket_path))),
      registry_auth_file_(std::move(registry_auth_file)) {}

HttpResponse DockerClient::call(const std::string& method, const std::string& target,
                                const std::string& json_body, std::chrono::milliseconds timeout) {
    HttpRequest request;
    request.method = method;
    request.target = target;
    request.body = json_body;
    if (!json_body.empty()) {
        request.headers["Content-Type"] = "application/json";
    }
    return send_request(endpoint_, request, timeout);
}

bool DockerClient::ping() {
    try {
        const auto response = call("GET", "/_ping", "", std::chrono::seconds(5));
        return response.status == 200 && response.body == "OK";
    } catch (const HttpError& e) {
        log::warn(std::string("docker ping failed: ") + e.what());
        return false;
    }
}

void DockerClient::ensure_image(const std::string& image) {
    const auto inspect = call("GET", "/images/" + image + "/json");
    if (inspect.status == 200) {
        return;
    }
    if (inspect.status != 404) {
        throw RuntimeError("inspect image " + image + " failed: " + describe(inspect));
    }

    log::info("pulling image " + image);
    const auto [name, tag] = split_image(image);
    HttpRequest request;
    request.method = "POST";
    request.target = "/images/create?fromImage=" + url_encode(name) + "&tag=" + url_encode(tag);
    if (!registry_auth_file_.empty()) {
        request.headers["X-Registry-Auth"] = base64_encode(read_file(registry_auth_file_));
    }
    const auto response = send_request(endpoint_, request, std::chrono::minutes(10));
    if (response.status != 200) {
        throw RuntimeError("pull " + image + " failed: " + describe(response));
    }
    // The pull streams JSON progress messages; a failure mid-stream still
    // returns 200 with an "error" field in one of the messages.
    std::size_t pos = 0;
    while (pos < response.body.size()) {
        std::size_t end = response.body.find('\n', pos);
        if (end == std::string::npos) {
            end = response.body.size();
        }
        const auto line = response.body.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) {
            continue;
        }
        const auto message = json::parse(line, nullptr, false);
        if (!message.is_discarded() && message.contains("error")) {
            throw RuntimeError("pull " + image + " failed: " + message["error"].get<std::string>());
        }
    }
}

std::string DockerClient::build_create_body(const ContainerSpec& spec, const std::string& user) {
    json env = json::array();
    for (const auto& [key, value] : spec.env) {
        env.push_back(key + "=" + value);
    }
    env.push_back(std::string("CHECKPOINT_DIR=") + kContainerCheckpointDir);

    json labels = json::object();
    for (const auto& [key, value] : spec.labels) {
        labels[key] = value;
    }

    json body = {
        {"Image", spec.image},
        {"Env", env},
        {"Labels", labels},
        {"StopSignal", "SIGTERM"},
        {"HostConfig",
         {
             {"Binds", json::array({spec.checkpoint_dir + ":" + kContainerCheckpointDir})},
             // Keep the container after exit so we can read its exit code;
             // the manager removes it explicitly.
             {"AutoRemove", false},
         }},
    };
    if (!user.empty()) {
        // Run as the manager's uid:gid so files the job writes into the shared
        // directory are owned by, and removable by, the manager.
        body["User"] = user;
    }
    return body.dump();
}

std::string DockerClient::start(const ContainerSpec& spec) {
    // A container with this name can survive a manager crash; remove it so the
    // create below does not fail with a name conflict.
    call("DELETE", "/containers/" + url_encode(spec.name) + "?force=true");

    const std::string user = std::to_string(::getuid()) + ":" + std::to_string(::getgid());
    const auto created = call("POST", "/containers/create?name=" + url_encode(spec.name),
                              build_create_body(spec, user));
    if (created.status != 201) {
        throw RuntimeError("create container " + spec.name + " failed: " + describe(created));
    }
    const std::string id = json::parse(created.body).at("Id").get<std::string>();

    const auto started = call("POST", "/containers/" + id + "/start");
    if (started.status != 204 && started.status != 304) {
        call("DELETE", "/containers/" + id + "?force=true");
        throw RuntimeError("start container " + spec.name + " failed: " + describe(started));
    }
    log::debug("started container " + spec.name + " (" + id.substr(0, 12) + ")");
    return id;
}

ContainerStatus DockerClient::inspect(const std::string& handle) {
    const auto response = call("GET", "/containers/" + handle + "/json");
    if (response.status == 404) {
        return ContainerStatus{};
    }
    if (response.status != 200) {
        throw RuntimeError("inspect container failed: " + describe(response));
    }
    const auto body = json::parse(response.body);
    const auto& state = body.at("State");
    ContainerStatus status;
    status.exists = true;
    status.running = state.at("Running").get<bool>();
    status.exit_code = state.at("ExitCode").get<int>();
    return status;
}

void DockerClient::stop(const std::string& handle, std::chrono::seconds grace) {
    // The daemon sends SIGTERM, waits `t` seconds, then SIGKILL, and only
    // replies once the container has stopped, so allow extra time.
    const auto response = call("POST", "/containers/" + handle + "/stop?t=" +
                                           std::to_string(grace.count()),
                               "", grace + std::chrono::seconds(15));
    if (response.status != 204 && response.status != 304 && response.status != 404) {
        throw RuntimeError("stop container failed: " + describe(response));
    }
}

void DockerClient::remove(const std::string& handle) {
    const auto response = call("DELETE", "/containers/" + handle + "?force=true");
    if (response.status != 204 && response.status != 404) {
        log::warn("remove container " + handle.substr(0, 12) + " failed: " + describe(response));
    }
}

std::string DockerClient::demultiplex_logs(const std::string& raw) {
    // Non-TTY containers frame each chunk with an 8-byte header:
    //   [stream(1) 0 0 0][size(4, big-endian)]
    std::string out;
    std::size_t pos = 0;
    while (pos + 8 <= raw.size()) {
        const auto b = [&](std::size_t i) { return static_cast<unsigned char>(raw[pos + i]); };
        if (b(0) > 2 || b(1) != 0 || b(2) != 0 || b(3) != 0) {
            return raw;  // not multiplexed (for example a TTY container)
        }
        const std::size_t size = (static_cast<std::size_t>(b(4)) << 24U) |
                                  (static_cast<std::size_t>(b(5)) << 16U) |
                                  (static_cast<std::size_t>(b(6)) << 8U) | b(7);
        pos += 8;
        out.append(raw, pos, std::min(size, raw.size() - pos));
        pos += size;
    }
    return out;
}

std::string DockerClient::logs_tail(const std::string& handle, int lines) {
    const auto response = call("GET", "/containers/" + handle +
                                          "/logs?stdout=1&stderr=1&tail=" + std::to_string(lines));
    if (response.status != 200) {
        return "";
    }
    return demultiplex_logs(response.body);
}

void DockerClient::remove_orphans(const std::string& manager_id) {
    const json filters = {{"label", json::array({std::string(kManagerLabel) + "=" + manager_id})}};
    const auto response = call("GET", "/containers/json?all=1&filters=" + url_encode(filters.dump()));
    if (response.status != 200) {
        log::warn("listing orphaned containers failed: " + describe(response));
        return;
    }
    for (const auto& container : json::parse(response.body)) {
        const auto id = container.at("Id").get<std::string>();
        log::warn("removing orphaned container " + id.substr(0, 12));
        remove(id);
    }
}

}  // namespace spot_runner
