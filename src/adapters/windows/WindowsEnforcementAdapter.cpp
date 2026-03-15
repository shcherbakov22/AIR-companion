#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

void WindowsEnforcementAdapter::applyPolicy(const models::DevicePolicy& policy) {
    switch (policy.internetAccessMode) {
        case models::InternetAccessMode::AllowAll:
            m_lastState = "internet allow_all";
            break;
        case models::InternetAccessMode::BlockAll:
            m_lastState = "internet block_all";
            break;
        case models::InternetAccessMode::AllowListOnly:
            m_lastState = "internet allow_list_only";
            break;
    }
}

void WindowsEnforcementAdapter::terminateBlockedApps(const std::vector<std::string>& blockedApps) {
    if (!blockedApps.empty()) {
        m_lastState += " + blocked apps";
    }
}

std::string WindowsEnforcementAdapter::describeState() const {
    return m_lastState;
}

}  // namespace companion::adapters::windows

