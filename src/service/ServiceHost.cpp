#include "companion/service/ServiceHost.h"

namespace companion::service {

ServiceHost::ServiceHost(core::Agent& agent) : m_agent(agent) {}

int ServiceHost::run() {
    m_agent.start();
    m_agent.tick();
    return 0;
}

}  // namespace companion::service

