#pragma once

#include <string>

#include "companion/core/Agent.h"

namespace companion::tray {

class TrayApplication {
public:
    explicit TrayApplication(core::Agent& agent);

    int run();
    std::string currentStatus() const;

private:
    core::Agent& m_agent;
};

}  // namespace companion::tray
