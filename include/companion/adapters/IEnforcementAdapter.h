#pragma once

#include <string>
#include <vector>

#include "companion/models/DevicePolicy.h"

namespace companion::adapters {

class IEnforcementAdapter {
public:
    virtual ~IEnforcementAdapter() = default;

    virtual void applyPolicy(const models::DevicePolicy& policy) = 0;
    virtual void terminateBlockedApps(const std::vector<std::string>& blockedApps) = 0;
    virtual std::string describeState() const = 0;
};

}  // namespace companion::adapters

