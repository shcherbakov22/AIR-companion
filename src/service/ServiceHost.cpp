#include "companion/service/ServiceHost.h"

#include <chrono>
#include <thread>

namespace companion::service {

ServiceHost::ServiceHost(core::Agent& agent) : m_agent(agent) {}

int ServiceHost::run() {
    m_agent.start();
    while (m_agent.running()) {
        m_agent.tick();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}

}  // namespace companion::service
