#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/core/Agent.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"
#include "companion/networking/CompanionApiClient.h"
#include "companion/service/ServiceHost.h"

int main() {
    companion::adapters::windows::WindowsAppTrackerAdapter appTrackerAdapter;
    companion::adapters::windows::WindowsBrowserDomainAdapter browserDomainAdapter;
    companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
    companion::adapters::windows::WindowsCameraCaptureAdapter cameraCaptureAdapter;
    companion::adapters::windows::WindowsEnforcementAdapter enforcementAdapter;

    companion::networking::CompanionApiClient apiClient("https://127.0.0.1");
    companion::core::PolicySync policySync(apiClient, "stub-device-token");
    companion::core::CommandPoller commandPoller(apiClient, "stub-device-token");
    companion::core::CaptureScheduler captureScheduler;
    companion::core::EnforcementCoordinator enforcementCoordinator(enforcementAdapter);
    companion::core::Agent agent(
        std::move(policySync),
        std::move(commandPoller),
        std::move(captureScheduler),
        std::move(enforcementCoordinator),
        appTrackerAdapter,
        browserDomainAdapter,
        screenCaptureAdapter,
        cameraCaptureAdapter
    );

    companion::service::ServiceHost serviceHost(agent);
    return serviceHost.run();
}
