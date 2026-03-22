#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/core/Agent.h"
#include "companion/core/CaptureScheduler.h"
#include "companion/core/CommandPoller.h"
#include "companion/core/EnforcementCoordinator.h"
#include "companion/core/PolicySync.h"
#include "companion/networking/CompanionApiClient.h"
#include "companion/service/Bootstrap.h"
#include "companion/service/CaptureSettingsStore.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/service/ServiceHost.h"
#include "companion/service/UpdateCoordinator.h"

#include <chrono>
#include <iostream>

int main() {
    companion::service::ServiceHost serviceHost([](companion::service::ServiceHost& host) {
        while (!host.stopRequested()) {
            companion::service::Bootstrap bootstrap;
            const auto bootstrapped = bootstrap.initialize();
            if (!bootstrapped.has_value()) {
                companion::service::EnrollmentRequestStore enrollmentRequestStore;
                const auto requestPath = enrollmentRequestStore.requestPath();
                (void) enrollmentRequestStore.saveTemplate();
                host.setLastStatus("bootstrap failed; retrying");
                std::cerr << "AIR Companion service bootstrap failed. Write enrollment details to " << requestPath
                          << " using air_companion_tray --write-enrollment --base-url <url> --username <name> --password <password>"
                          << " [--device-label <label>] [--root-ca-url <url>] or set AIR_COMPANION_USERNAME, AIR_COMPANION_PASSWORD,"
                          << " and optional AIR_COMPANION_ROOT_CA_URL for first enrollment." << '\n';
                if (host.waitForStop(15000)) {
                    break;
                }
                continue;
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
            companion::service::UpdateCoordinator updateCoordinator(bootstrapped->apiClient, AIR_COMPANION_VERSION);
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
                std::move(updateCoordinator),
                appTrackerAdapter,
                browserDomainAdapter,
                networkConfigurationAdapter,
                remoteAccessAdapter,
                screenCaptureAdapter,
                cameraCaptureAdapter
            );

            std::cout << bootstrapped->status << '\n';
            host.setLastStatus(bootstrapped->status);
            agent.start();

            try {
                while (!host.stopRequested() && agent.running()) {
                    agent.tick();
                    host.setLastStatus(agent.statusSummary());

                    const auto waitMs = host.consumeResumeRequested() ? 0UL : 1000UL;
                    if (host.waitForStop(waitMs)) {
                        break;
                    }
                }
            } catch (const std::exception& exception) {
                std::cerr << "AIR Companion agent loop exception: " << exception.what() << '\n';
                host.setLastStatus(std::string("agent error: ") + exception.what());
            } catch (...) {
                std::cerr << "AIR Companion agent loop exception: unknown\n";
                host.setLastStatus("agent error: unknown");
            }

            agent.stop();
            if (host.stopRequested()) {
                break;
            }

            if (host.waitForStop(5000)) {
                break;
            }
        }
    });

    return serviceHost.run();
}
