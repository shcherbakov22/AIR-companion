#pragma once

#include "companion/adapters/IEnforcementAdapter.h"
#include "companion/models/DeviceCommand.h"
#include "companion/models/DevicePolicy.h"

namespace companion::core {

class EnforcementCoordinator {
public:
    explicit EnforcementCoordinator(adapters::IEnforcementAdapter& enforcementAdapter);

    void applyPolicy(const models::DevicePolicy& policy);
    void applyCommand(const models::DeviceCommand& command);

private:
    adapters::IEnforcementAdapter& m_enforcementAdapter;
};

}  // namespace companion::core

