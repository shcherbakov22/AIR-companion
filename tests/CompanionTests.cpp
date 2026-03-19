#include "companion/core/CaptureScheduler.h"
#include "companion/models/DevicePolicy.h"
#include "companion/networking/CompanionApiParsers.h"
#include "companion/service/BootAutoStartRegistrar.h"
#include "companion/service/CompanionConfigStore.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/service/TrustedRootInstaller.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T>
void requireEqual(const T& actual, const T& expected, const std::string& message) {
    if (!(actual == expected)) {
        std::ostringstream out;
        out << message << " expected=" << expected << " actual=" << actual;
        throw TestFailure(out.str());
    }
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* name, const std::string& value) : m_name(name) {
        if (const char* existing = std::getenv(name); existing != nullptr) {
            m_original = std::string(existing);
        }

#ifdef _WIN32
        _putenv_s(name, value.c_str());
#else
        setenv(name, value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
#ifdef _WIN32
        _putenv_s(m_name.c_str(), m_original.has_value() ? m_original->c_str() : "");
#else
        if (m_original.has_value()) {
            setenv(m_name.c_str(), m_original->c_str(), 1);
        } else {
            unsetenv(m_name.c_str());
        }
#endif
    }

private:
    std::string m_name;
    std::optional<std::string> m_original;
};

class ScopedTempDir {
public:
    ScopedTempDir() {
        std::mt19937_64 generator(std::random_device{}());
        std::uniform_int_distribution<unsigned long long> distribution;
        m_path = fs::temp_directory_path() / "air-companion-tests" / fs::path(std::to_string(distribution(generator)));
        fs::create_directories(m_path);
    }

    ~ScopedTempDir() {
        std::error_code errorCode;
        fs::remove_all(m_path, errorCode);
    }

    const fs::path& path() const {
        return m_path;
    }

private:
    fs::path m_path;
};

void testParseEnrollmentResponse() {
    companion::models::DeviceIdentity identity;
    identity.deviceId = "device-alpha";
    identity.hostname = "old-host";
    identity.deviceLabel = "old-label";
    identity.platform = "windows";
    identity.appVersion = "0.1.0";

    const std::string body = R"({
        "accepted": true,
        "token": "token-123",
        "student": {"username": "ego"},
        "device": {"label": "Desk PC", "hostname": "desk-pc"}
    })";

    const auto enrollment = companion::networking::parseEnrollmentResponse(body, identity, "fallback-user");
    require(enrollment.has_value(), "enrollment should parse");
    requireEqual(enrollment->deviceToken, std::string("token-123"), "device token");
    requireEqual(enrollment->identity.studentUsername, std::string("ego"), "student username");
    requireEqual(enrollment->identity.deviceLabel, std::string("Desk PC"), "device label");
    requireEqual(enrollment->identity.hostname, std::string("desk-pc"), "hostname");
}

void testParsePolicyResponse() {
    const std::string body = R"({
        "policy_hash": "hash-1",
        "policy": {
            "student": {"display_name": "Ego"},
            "schedule": {"name": "Morning"},
            "task": {"title": "Coding"},
            "communication_gate": {"has_unread_chat": true, "has_unread_announcements": false},
            "violations": {"open_count": 1},
            "capture": {"screen_enabled": true, "camera_enabled": true, "screen_interval_seconds": 45, "camera_interval_seconds": 90},
            "internet_policy": {"mode": "allow_all"}
        }
    })";

    const auto policy = companion::networking::parsePolicyResponse(body);
    require(policy.has_value(), "policy should parse");
    requireEqual(policy->policyHash, std::string("hash-1"), "policy hash");
    requireEqual(policy->studentDisplayName, std::string("Ego"), "student name");
    requireEqual(policy->activeScheduleName, std::string("Morning"), "schedule name");
    requireEqual(policy->activeTaskName, std::string("Coding"), "task name");
    require(policy->hasUnreadMentorChat, "chat gate should parse");
    require(!policy->hasUnreadAnnouncements, "announcement gate should parse");
    require(policy->hasOpenViolations, "violations should parse");
    require(policy->shouldCaptureScreen, "screen capture enabled");
    require(policy->shouldCaptureCamera, "camera capture enabled");
    requireEqual(policy->screenCaptureIntervalSeconds, 45, "screen interval");
    requireEqual(policy->cameraCaptureIntervalSeconds, 90, "camera interval");
    require(policy->internetAccessMode == companion::models::InternetAccessMode::AllowAll, "internet mode");
}

void testParseCommandResponseSupportsNumericIds() {
    const std::string body = R"({
        "accepted": true,
        "command": {
            "id": 3,
            "command_type": "request_camera_capture",
            "status": "pending",
            "payload": {"source": "manual_debug"}
        }
    })";

    const auto commands = companion::networking::parseCommandResponse(body);
    requireEqual(commands.size(), static_cast<std::size_t>(1), "command count");
    requireEqual(commands.front().id, std::string("3"), "numeric command id");
    require(commands.front().type == companion::models::DeviceCommandType::RequestCameraCapture, "command type");
    requireEqual(commands.front().status, std::string("pending"), "command status");
    requireEqual(commands.front().payloadJson, std::string("{\"source\": \"manual_debug\"}"), "payload object parse");
}

void testCompanionConfigStoreRoundTrip() {
    ScopedTempDir tempDir;
    ScopedEnvVar appData("APPDATA", tempDir.path().string());

    companion::service::CompanionConfigStore store;
    companion::service::StoredCompanionConfig config;
    config.baseUrl = "https://192.168.11.228";
    config.deviceToken = "token-abc";
    config.rootCaUrl = "https://192.168.11.228/companion/root-ca.crt";
    config.identity.deviceId = "device-1";
    config.identity.hostname = "WINDOWS-TEST";
    config.identity.deviceLabel = "Desk PC";
    config.identity.platform = "windows";
    config.identity.appVersion = "0.1.0";
    config.identity.studentUsername = "ego";

    require(store.save(config), "config save");
    const auto loaded = store.load();
    require(loaded.has_value(), "config load");
    requireEqual(loaded->baseUrl, config.baseUrl, "config base url");
    requireEqual(loaded->deviceToken, config.deviceToken, "config token");
    requireEqual(loaded->rootCaUrl, config.rootCaUrl, "config root ca url");
    requireEqual(loaded->identity.deviceId, config.identity.deviceId, "config device id");
    require(store.clear(), "config clear");
    require(!store.load().has_value(), "config should clear");
}

void testEnrollmentRequestStoreRoundTrip() {
    ScopedTempDir tempDir;
    ScopedEnvVar programData("PROGRAMDATA", tempDir.path().string());

    companion::service::EnrollmentRequestStore store;
    companion::service::EnrollmentRequest request{
        "https://192.168.11.228",
        "ego",
        "0",
        "codex-pc",
        "https://192.168.11.228/companion/root-ca.crt",
    };

    require(store.save(request), "request save");
    const auto draft = store.loadDraft();
    require(draft.has_value(), "draft load");
    requireEqual(draft->baseUrl, request.baseUrl, "draft base url");
    requireEqual(draft->username, request.username, "draft username");
    requireEqual(draft->password, request.password, "draft password");
    requireEqual(draft->deviceLabel, request.deviceLabel, "draft label");
    requireEqual(draft->rootCaUrl, request.rootCaUrl, "draft root ca url");

    const auto loaded = store.load();
    require(loaded.has_value(), "request load");
    requireEqual(loaded->username, request.username, "request username");
    requireEqual(loaded->rootCaUrl, request.rootCaUrl, "request root ca url");
    require(store.clear(), "request clear");
    require(!store.loadDraft().has_value(), "request should clear");
}

void testCaptureSchedulerRespectsMinimumsAndMarks() {
    companion::service::InternalCaptureSettings settings;
    settings.allowScreenCapture = true;
    settings.allowCameraCapture = true;
    settings.minimumScreenIntervalSeconds = 30;
    settings.minimumCameraIntervalSeconds = 20;

    companion::core::CaptureScheduler scheduler(settings);

    companion::models::DevicePolicy policy;
    policy.shouldCaptureScreen = true;
    policy.shouldCaptureCamera = true;
    policy.screenCaptureIntervalSeconds = 5;
    policy.cameraCaptureIntervalSeconds = 10;
    scheduler.updatePolicy(policy);

    const auto start = std::chrono::steady_clock::now();
    require(scheduler.shouldCaptureScreen(start), "initial screen capture");
    require(scheduler.shouldCaptureCamera(start), "initial camera capture");

    scheduler.markScreenCaptured(start);
    scheduler.markCameraCaptured(start);

    require(!scheduler.shouldCaptureScreen(start + std::chrono::seconds(10)), "screen interval minimum");
    require(!scheduler.shouldCaptureCamera(start + std::chrono::seconds(10)), "camera interval minimum");
    require(scheduler.shouldCaptureScreen(start + std::chrono::seconds(30)), "screen interval reached");
    require(scheduler.shouldCaptureCamera(start + std::chrono::seconds(20)), "camera interval reached");
}

void testTrustedRootInstallerDerivesCertificateUrl() {
    requireEqual(
        companion::service::TrustedRootInstaller::rootCertificateUrlForBaseUrl("https://192.168.11.228"),
        std::string("https://192.168.11.228/companion/root-ca.crt"),
        "https base url should derive https root cert url"
    );

    requireEqual(
        companion::service::TrustedRootInstaller::rootCertificateUrlForBaseUrl("http://192.168.11.228"),
        std::string("http://192.168.11.228/companion/root-ca.crt"),
        "http base url should stay http"
    );

    requireEqual(
        companion::service::TrustedRootInstaller::rootCertificateUrlForBaseUrl("https://192.168.11.228:8443"),
        std::string("https://192.168.11.228:8443/companion/root-ca.crt"),
        "custom https port should be preserved"
    );
}

void testBootAutoStartRegistrarResolvesServiceBinaryPath() {
    requireEqual(
        companion::service::BootAutoStartRegistrar::taskName(),
        std::string("AIR Companion"),
        "boot task name"
    );

    requireEqual(
        companion::service::BootAutoStartRegistrar::serviceBinaryPathForExecutable(
            "C:\\Users\\user\\codex\\air-companion\\build\\windows-debug\\Debug\\air_companion_tray.exe"
        ),
        std::string("C:\\Users\\user\\codex\\air-companion\\build\\windows-debug\\Debug\\air_companion_service.exe"),
        "tray executable should resolve to sibling service executable"
    );

    requireEqual(
        companion::service::BootAutoStartRegistrar::serviceBinaryPathForExecutable(
            "C:\\Users\\user\\codex\\air-companion\\build\\windows-debug\\Debug\\air_companion_service.exe"
        ),
        std::string("C:\\Users\\user\\codex\\air-companion\\build\\windows-debug\\Debug\\air_companion_service.exe"),
        "service executable should preserve itself"
    );
}

}  // namespace

int main() {
    struct TestCase {
        const char* name;
        std::function<void()> run;
    };

    const std::vector<TestCase> tests = {
        {"parseEnrollmentResponse", testParseEnrollmentResponse},
        {"parsePolicyResponse", testParsePolicyResponse},
        {"parseCommandResponseSupportsNumericIds", testParseCommandResponseSupportsNumericIds},
        {"companionConfigStoreRoundTrip", testCompanionConfigStoreRoundTrip},
        {"enrollmentRequestStoreRoundTrip", testEnrollmentRequestStoreRoundTrip},
        {"captureSchedulerRespectsMinimumsAndMarks", testCaptureSchedulerRespectsMinimumsAndMarks},
        {"trustedRootInstallerDerivesCertificateUrl", testTrustedRootInstallerDerivesCertificateUrl},
        {"bootAutoStartRegistrarResolvesServiceBinaryPath", testBootAutoStartRegistrarResolvesServiceBinaryPath},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        }
    }

    return failures == 0 ? 0 : 1;
}
