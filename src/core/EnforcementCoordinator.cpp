#include "companion/core/EnforcementCoordinator.h"

namespace companion::core {

EnforcementCoordinator::EnforcementCoordinator(adapters::IEnforcementAdapter& enforcementAdapter)
    : m_enforcementAdapter(enforcementAdapter) {}

void EnforcementCoordinator::applyPolicy(const models::DevicePolicy& policy) {
    m_enforcementAdapter.applyPolicy(policy);
    m_enforcementAdapter.terminateBlockedApps(policy.blockedApps);
}

void EnforcementCoordinator::applyCommand(const models::DeviceCommand& command) {
    switch (command.type) {
        case models::DeviceCommandType::LockInternet:
        case models::DeviceCommandType::UnlockInternet:
        case models::DeviceCommandType::RefreshPolicy:
        case models::DeviceCommandType::RequestScreenshot:
        case models::DeviceCommandType::RequestCameraCapture:
        case models::DeviceCommandType::Unknown:
        default:
            break;
    }
}

}  // namespace companion::core

