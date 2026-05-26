#pragma once

#include <string>
#include <vector>

#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DevicePolicy.h"

namespace companion::adapters {

class IEnforcementAdapter {
public:
    virtual ~IEnforcementAdapter() = default;

    virtual void applyPolicy(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot) = 0;
    virtual std::vector<std::string> terminateBlockedApps(const std::vector<std::string>& blockedApps) = 0;
    virtual bool showMessage(const std::string& title, const std::string& body, int displaySeconds, std::string& error) = 0;
    virtual std::string describeState() const = 0;
};

}  // namespace companion::adapters

