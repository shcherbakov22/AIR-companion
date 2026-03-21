#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/tray/RemoteControlHelper.h"

#include <iostream>

namespace {

bool hasArgument(int argc, char* argv[], const char* expected) {
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == expected) {
            return true;
        }
    }

    return false;
}

const char* argumentValue(int argc, char* argv[], const char* expected) {
    for (int index = 1; index < argc - 1; ++index) {
        if (std::string(argv[index]) == expected) {
            return argv[index + 1];
        }
    }

    return nullptr;
}

int printUsage() {
    std::cerr
        << "AIR Companion utility modes:\n"
        << "  --capture-screen-once <output-directory>\n"
        << "  --snapshot-apps-once <output-file>\n"
        << "  --remote-helper --port <port> --state-file <path>\n"
        << "  --write-enrollment --base-url <url> --username <name> --password <password> [--device-label <label>] [--root-ca-url <url>]\n"
        << "  --print-enrollment-path\n"
        << "  --clear-enrollment\n";
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (const auto* outputDirectory = argumentValue(argc, argv, "--capture-screen-once"); outputDirectory != nullptr) {
        companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
        return screenCaptureAdapter.captureToFile(outputDirectory).has_value() ? 0 : 1;
    }

    if (const auto* outputFile = argumentValue(argc, argv, "--snapshot-apps-once"); outputFile != nullptr) {
        companion::adapters::windows::WindowsAppTrackerAdapter appTrackerAdapter;
        return appTrackerAdapter.writeSnapshotToFile(outputFile) ? 0 : 1;
    }

    if (hasArgument(argc, argv, "--remote-helper")) {
        const auto* portValue = argumentValue(argc, argv, "--port");
        const auto* stateFilePath = argumentValue(argc, argv, "--state-file");
        if (portValue == nullptr || stateFilePath == nullptr) {
            return 1;
        }

        return companion::tray::runRemoteControlHelper(std::stoi(portValue), stateFilePath);
    }

    companion::service::EnrollmentRequestStore enrollmentRequestStore;

    if (hasArgument(argc, argv, "--print-enrollment-path")) {
        std::cout << enrollmentRequestStore.requestPath() << '\n';
        return 0;
    }

    if (hasArgument(argc, argv, "--clear-enrollment")) {
        return enrollmentRequestStore.clear() ? 0 : 1;
    }

    if (hasArgument(argc, argv, "--write-enrollment")) {
        const auto* baseUrl = argumentValue(argc, argv, "--base-url");
        const auto* username = argumentValue(argc, argv, "--username");
        const auto* password = argumentValue(argc, argv, "--password");
        if (baseUrl == nullptr || username == nullptr || password == nullptr) {
            return printUsage();
        }

        companion::service::EnrollmentRequest request{
            .baseUrl = baseUrl,
            .username = username,
            .password = password,
            .deviceLabel = argumentValue(argc, argv, "--device-label") != nullptr
                ? argumentValue(argc, argv, "--device-label")
                : std::string{},
            .rootCaUrl = argumentValue(argc, argv, "--root-ca-url") != nullptr
                ? argumentValue(argc, argv, "--root-ca-url")
                : std::string{},
        };

        if (!enrollmentRequestStore.save(request)) {
            return 1;
        }

        std::cout << enrollmentRequestStore.requestPath() << '\n';
        return 0;
    }

    return printUsage();
}
