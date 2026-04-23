#include "companion/adapters/linux/LinuxAdapters.h"
#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DeviceCommand.h"
#include "companion/networking/CompanionApiClient.h"
#include "companion/service/CaptureSettingsStore.h"
#include "companion/service/CompanionConfigStore.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::atomic_bool g_running{true};

void signalHandler(int) {
    g_running = false;
}

std::optional<std::string> envValue(const char* name) {
    const auto* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

int envInt(const char* name, int fallback) {
    const auto value = envValue(name);
    if (!value.has_value()) {
        return fallback;
    }

    try {
        return std::stoi(*value);
    } catch (...) {
        return fallback;
    }
}

std::string hostname() {
    char buffer[256]{};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0 && buffer[0] != '\0') {
        return buffer;
    }
    return "linux-companion";
}

std::filesystem::path stateBaseDirectory() {
    if (const auto value = envValue("XDG_STATE_HOME"); value.has_value()) {
        return *value;
    }
    if (const auto home = envValue("HOME"); home.has_value()) {
        return std::filesystem::path(*home) / ".local" / "state";
    }
    return ".";
}

std::string defaultScreenDirectory() {
    return (stateBaseDirectory() / "AIRCompanion" / "Captures" / "screen").string();
}

companion::models::DeviceIdentity fallbackIdentity() {
    companion::models::DeviceIdentity identity;
    identity.deviceId = envValue("AIR_COMPANION_DEVICE_ID").value_or(hostname());
    identity.hostname = envValue("AIR_COMPANION_HOSTNAME").value_or(hostname());
    identity.deviceLabel = envValue("AIR_COMPANION_DEVICE_LABEL").value_or(identity.hostname);
    identity.platform = "linux";
    identity.appVersion = AIR_COMPANION_VERSION;
    identity.studentUsername = envValue("AIR_COMPANION_STUDENT_USERNAME").value_or({});
    return identity;
}

struct RuntimeConfig {
    std::string baseUrl;
    std::string deviceToken;
    companion::models::DeviceIdentity identity;
};

std::optional<RuntimeConfig> loadRuntimeConfig() {
    companion::service::CompanionConfigStore store;
    auto stored = store.load();

    RuntimeConfig config;
    if (stored.has_value()) {
        config.baseUrl = stored->baseUrl;
        config.deviceToken = stored->deviceToken;
        config.identity = stored->identity;
    } else {
        config.identity = fallbackIdentity();
    }

    if (const auto baseUrl = envValue("AIR_COMPANION_BASE_URL"); baseUrl.has_value()) {
        config.baseUrl = *baseUrl;
    }
    if (const auto token = envValue("AIR_COMPANION_DEVICE_TOKEN"); token.has_value()) {
        config.deviceToken = *token;
    }
    if (const auto deviceId = envValue("AIR_COMPANION_DEVICE_ID"); deviceId.has_value()) {
        config.identity.deviceId = *deviceId;
    }
    if (const auto label = envValue("AIR_COMPANION_DEVICE_LABEL"); label.has_value()) {
        config.identity.deviceLabel = *label;
    }
    if (const auto student = envValue("AIR_COMPANION_STUDENT_USERNAME"); student.has_value()) {
        config.identity.studentUsername = *student;
    }
    if (config.identity.hostname.empty()) {
        config.identity.hostname = hostname();
    }
    if (config.identity.deviceId.empty()) {
        config.identity.deviceId = config.identity.hostname;
    }
    if (config.identity.deviceLabel.empty()) {
        config.identity.deviceLabel = config.identity.hostname;
    }
    config.identity.platform = "linux";
    config.identity.appVersion = AIR_COMPANION_VERSION;

    if (config.baseUrl.empty() || config.deviceToken.empty()) {
        return std::nullopt;
    }

    return config;
}

companion::models::ActivitySnapshot linuxSnapshot() {
    companion::models::ActivitySnapshot snapshot;
    snapshot.focusedApp = "linux-desktop";
    snapshot.focusedWindowTitle = "Linux desktop";
    snapshot.activeBrowserDomain = {};
    return snapshot;
}

bool captureAndUpload(companion::adapters::linux::LinuxScreenCaptureAdapter& captureAdapter,
                      const companion::networking::CompanionApiClient& api,
                      const std::string& deviceToken,
                      const std::string& outputDirectory) {
    const auto path = captureAdapter.captureToFile(outputDirectory);
    if (!path.has_value()) {
        std::cerr << "screen capture failed\n";
        return false;
    }

    const auto uploaded = api.uploadScreenCapture(deviceToken, *path, linuxSnapshot(), "image/jpeg");
    std::cout << "screen capture " << (uploaded ? "uploaded" : "upload failed") << ": " << *path << '\n';
    return uploaded;
}

void printUsage(const char* argv0) {
    std::cout
        << "Usage:\n"
        << "  " << argv0 << " --capture-once <directory>\n"
        << "  " << argv0 << " [--upload-once]\n\n"
        << "Config:\n"
        << "  Uses ~/.config/AIRCompanion/config.json or /etc/air-companion/config.json.\n"
        << "  Env overrides: AIR_COMPANION_BASE_URL, AIR_COMPANION_DEVICE_TOKEN,\n"
        << "  AIR_COMPANION_SCREEN_INTERVAL_SECONDS, AIR_COMPANION_SCREEN_OUTPUT_DIR.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    companion::adapters::linux::LinuxScreenCaptureAdapter captureAdapter;

    if (argc >= 2 && std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--capture-once") {
        const auto path = captureAdapter.captureToFile(argv[2]);
        if (!path.has_value()) {
            std::cerr << "screen capture failed\n";
            return 2;
        }
        std::cout << *path << '\n';
        return 0;
    }

    const auto runtime = loadRuntimeConfig();
    if (!runtime.has_value()) {
        std::cerr << "missing companion config; set AIR_COMPANION_BASE_URL and AIR_COMPANION_DEVICE_TOKEN\n";
        printUsage(argv[0]);
        return 2;
    }

    companion::networking::CompanionApiClient api(runtime->baseUrl);
    const auto outputDirectory = envValue("AIR_COMPANION_SCREEN_OUTPUT_DIR").value_or(defaultScreenDirectory());
    const bool uploadOnce = argc >= 2 && std::string(argv[1]) == "--upload-once";

    if (uploadOnce) {
        return captureAndUpload(captureAdapter, api, runtime->deviceToken, outputDirectory) ? 0 : 3;
    }

    auto interval = std::chrono::seconds(envInt("AIR_COMPANION_SCREEN_INTERVAL_SECONDS", 30));
    if (interval < std::chrono::seconds(5)) {
        interval = std::chrono::seconds(5);
    }

    auto nextPolicyAt = std::chrono::steady_clock::time_point{};
    auto nextCommandAt = std::chrono::steady_clock::time_point{};
    auto nextHeartbeatAt = std::chrono::steady_clock::time_point{};
    auto nextCaptureAt = std::chrono::steady_clock::now();
    bool shouldCapture = true;

    std::cout << "AIR companion Linux screenshot service started for " << runtime->baseUrl << '\n';

    while (g_running) {
        const auto now = std::chrono::steady_clock::now();

        if (now >= nextPolicyAt) {
            if (const auto policy = api.fetchPolicy(runtime->deviceToken); policy.has_value()) {
                shouldCapture = policy->shouldCaptureScreen;
                if (policy->screenCaptureIntervalSeconds > 0) {
                    interval = std::chrono::seconds(policy->screenCaptureIntervalSeconds);
                    if (interval < std::chrono::seconds(5)) {
                        interval = std::chrono::seconds(5);
                    }
                }
            }
            nextPolicyAt = now + std::chrono::seconds(30);
        }

        if (now >= nextCommandAt) {
            for (const auto& command : api.fetchCommands(runtime->deviceToken)) {
                if (command.type != companion::models::DeviceCommandType::RequestScreenshot) {
                    continue;
                }

                api.acknowledgeCommand(runtime->deviceToken, command.id);
                const auto success = captureAndUpload(captureAdapter, api, runtime->deviceToken, outputDirectory);
                api.submitCommandResult(
                    runtime->deviceToken,
                    command.id,
                    success,
                    success ? "screen capture uploaded" : "screen capture failed"
                );
            }
            nextCommandAt = now + std::chrono::seconds(5);
        }

        if (now >= nextHeartbeatAt) {
            (void)api.sendHeartbeat(runtime->deviceToken, runtime->identity, linuxSnapshot(), "linux-screenshot-only");
            nextHeartbeatAt = now + std::chrono::seconds(30);
        }

        if (shouldCapture && now >= nextCaptureAt) {
            (void)captureAndUpload(captureAdapter, api, runtime->deviceToken, outputDirectory);
            nextCaptureAt = now + interval;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "AIR companion Linux screenshot service stopped\n";
    return 0;
}
