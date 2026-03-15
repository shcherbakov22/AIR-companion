#include "companion/tray/TrayApplication.h"

#include <iostream>

namespace companion::tray {

TrayApplication::TrayApplication(core::Agent& agent) : m_agent(agent) {}

int TrayApplication::run() {
    m_agent.start();
    m_agent.tick();
    std::cout << "AIR Companion tray started." << '\n';
    std::cout << currentStatus() << '\n';
    return 0;
}

std::string TrayApplication::currentStatus() const {
    return m_agent.statusSummary();
}

}  // namespace companion::tray
