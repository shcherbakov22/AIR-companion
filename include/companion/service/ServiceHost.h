#pragma once

#include <string>

#include "companion/core/Agent.h"

namespace companion::service {

class ServiceHost {
public:
    explicit ServiceHost(core::Agent& agent);

    int run();
    const std::string& lastStatus() const;

private:
    core::Agent& m_agent;
    std::string m_lastStatus{"idle"};
};

}  // namespace companion::service
