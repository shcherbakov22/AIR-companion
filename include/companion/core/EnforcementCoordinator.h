#pragma once

#include "companion/adapters/IEnforcementAdapter.h"
#include "companion/models/ActivitySnapshot.h"
#include "companion/models/DeviceCommand.h"
#include "companion/models/DevicePolicy.h"

namespace companion::core {

class EnforcementCoordinator {
public:
    struct CommandExecutionResult {
        bool success{true};
        std::string output{"completed"};
    };

    explicit EnforcementCoordinator(adapters::IEnforcementAdapter& enforcementAdapter);

    void applyPolicy(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot);
    CommandExecutionResult applyCommand(const models::DeviceCommand& command);

private:
    adapters::IEnforcementAdapter& m_enforcementAdapter;
};

}  // namespace companion::core

