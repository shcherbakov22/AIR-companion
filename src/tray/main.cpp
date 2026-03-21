#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/core/Agent.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"
#include "companion/networking/CompanionApiClient.h"
#include "companion/service/Bootstrap.h"
#include "companion/service/CaptureSettingsStore.h"
#include "companion/service/CompanionConfigStore.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/tray/EnrollmentWindow.h"
#include "companion/tray/RemoteControlHelper.h"
#include "companion/tray/TrayApplication.h"

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

companion::service::EnrollmentRequest initialEnrollmentRequest() {
    companion::service::EnrollmentRequestStore enrollmentRequestStore;
    companion::service::CompanionConfigStore configStore;

    auto draft = enrollmentRequestStore.loadDraft().value_or(companion::service::EnrollmentRequest{
        "https://192.168.11.228",
        {},
        {},
        {},
        {},
    });

    if (const auto stored = configStore.load(); stored.has_value()) {
        if (draft.baseUrl.empty()) {
            draft.baseUrl = stored->baseUrl;
        }
        if (draft.username.empty()) {
            draft.username = stored->identity.studentUsername;
        }
        if (draft.deviceLabel.empty()) {
            draft.deviceLabel = stored->identity.deviceLabel;
        }
        if (draft.rootCaUrl.empty()) {
            draft.rootCaUrl = stored->rootCaUrl;
        }
    }

    return draft;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (const auto* outputDirectory = argumentValue(argc, argv, "--capture-screen-once"); outputDirectory != nullptr) {
        companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
        return screenCaptureAdapter.captureToFile(outputDirectory).has_value() ? 0 : 1;
    }

    if (hasArgument(argc, argv, "--remote-helper")) {
        const auto* portValue = argumentValue(argc, argv, "--port");
        const auto* stateFilePath = argumentValue(argc, argv, "--state-file");
        if (portValue == nullptr || stateFilePath == nullptr) {
            return 1;
        }

        return companion::tray::runRemoteControlHelper(std::stoi(portValue), stateFilePath);
    }

    companion::service::Bootstrap bootstrap;
    companion::service::EnrollmentRequestStore enrollmentRequestStore;
    companion::service::CompanionConfigStore configStore;

    if (hasArgument(argc, argv, "--settings")) {
        const auto request = companion::tray::EnrollmentWindow::prompt(
            initialEnrollmentRequest(),
            "Update AIR enrollment details for this device."
        );

        if (!request.has_value() || !enrollmentRequestStore.save(*request)) {
            std::cerr << "AIR Companion settings cancelled." << '\n';
            return 1;
        }

        (void) configStore.clear();
    }

    auto bootstrapped = bootstrap.initialize();
    if (!bootstrapped.has_value()) {
        const auto request = companion::tray::EnrollmentWindow::prompt(
            initialEnrollmentRequest(),
            "Enter AIR credentials to enroll this device."
        );

        if (!request.has_value() || !enrollmentRequestStore.save(*request)) {
            std::cerr << "AIR Companion tray bootstrap cancelled." << '\n';
            return 1;
        }

        bootstrapped = bootstrap.initialize();
        if (!bootstrapped.has_value()) {
            std::cerr << "AIR Companion tray bootstrap failed after enrollment attempt." << '\n';
            return 1;
        }
    }

    companion::adapters::windows::WindowsServiceLifecycleAdapter serviceLifecycleAdapter;
    if (!serviceLifecycleAdapter.install()) {
        std::cerr << "AIR Companion could not install the Windows service for automatic startup." << '\n';
    }

    companion::adapters::windows::WindowsAppTrackerAdapter appTrackerAdapter;
    companion::adapters::windows::WindowsBrowserDomainAdapter browserDomainAdapter;
    companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
    companion::adapters::windows::WindowsCameraCaptureAdapter cameraCaptureAdapter;
    companion::adapters::windows::WindowsEnforcementAdapter enforcementAdapter;
    companion::adapters::windows::WindowsNetworkConfigurationAdapter networkConfigurationAdapter;
    companion::adapters::windows::WindowsRemoteAccessAdapter remoteAccessAdapter;
    companion::service::CaptureSettingsStore captureSettingsStore;
    const auto captureSettings = captureSettingsStore.loadOrCreate();

    companion::core::PolicySync policySync(bootstrapped->apiClient, bootstrapped->config.deviceToken);
    companion::core::CommandPoller commandPoller(bootstrapped->apiClient, bootstrapped->config.deviceToken);
    companion::core::CaptureScheduler captureScheduler(captureSettings);
    companion::core::EnforcementCoordinator enforcementCoordinator(enforcementAdapter);
    companion::core::UplinkSync uplinkSync(
        bootstrapped->apiClient,
        bootstrapped->config.deviceToken,
        bootstrapped->config.identity,
        networkConfigurationAdapter
    );
    companion::core::Agent agent(
        std::move(policySync),
        std::move(commandPoller),
        std::move(captureScheduler),
        std::move(enforcementCoordinator),
        std::move(uplinkSync),
        appTrackerAdapter,
        browserDomainAdapter,
        networkConfigurationAdapter,
        remoteAccessAdapter,
        screenCaptureAdapter,
        cameraCaptureAdapter
    );

    companion::tray::TrayApplication trayApplication(agent);
    std::cout << bootstrapped->status << '\n';
    return trayApplication.run();
}
