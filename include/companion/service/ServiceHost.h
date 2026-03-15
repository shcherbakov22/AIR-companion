#pragma once

#include "companion/core/Agent.h"

namespace companion::service {

class ServiceHost {
public:
    explicit ServiceHost(core::Agent& agent);

    int run();

private:
    core::Agent& m_agent;
};

}  // namespace companion::service

