#include "companion/tray/TrayApplication.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace companion::tray {

TrayApplication::TrayApplication(core::Agent& agent) : m_agent(agent) {}

int TrayApplication::run() {
    m_agent.start();
    std::cout << "AIR Companion tray started." << '\n';
    while (m_agent.running()) {
        m_agent.tick();
        std::cout << currentStatus() << '\n';
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
}

std::string TrayApplication::currentStatus() const {
    return m_agent.statusSummary();
}

}  // namespace companion::tray
