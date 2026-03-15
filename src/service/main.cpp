#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/core/Agent.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"
#include "companion/networking/CompanionApiClient.h"
#include "companion/service/Bootstrap.h"
#include "companion/service/ServiceHost.h"

#include <iostream>

int main() {
    companion::service::Bootstrap bootstrap;
    const auto bootstrapped = bootstrap.initialize();
    if (!bootstrapped.has_value()) {
        std::cerr << "AIR Companion service bootstrap failed. Set AIR_COMPANION_USERNAME and AIR_COMPANION_PASSWORD for first enrollment." << '\n';
        return 1;
    }

    companion::adapters::windows::WindowsAppTrackerAdapter appTrackerAdapter;
    companion::adapters::windows::WindowsBrowserDomainAdapter browserDomainAdapter;
    companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
    companion::adapters::windows::WindowsCameraCaptureAdapter cameraCaptureAdapter;
    companion::adapters::windows::WindowsEnforcementAdapter enforcementAdapter;
    companion::adapters::windows::WindowsNetworkConfigurationAdapter networkConfigurationAdapter;

    companion::core::PolicySync policySync(bootstrapped->apiClient, bootstrapped->config.deviceToken);
    companion::core::CommandPoller commandPoller(bootstrapped->apiClient, bootstrapped->config.deviceToken);
    companion::core::CaptureScheduler captureScheduler;
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
        screenCaptureAdapter,
        cameraCaptureAdapter
    );

    companion::service::ServiceHost serviceHost(agent);
    std::cout << bootstrapped->status << '\n';
    return serviceHost.run();
}
