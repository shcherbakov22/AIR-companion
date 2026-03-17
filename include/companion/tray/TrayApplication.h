#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "companion/core/Agent.h"

namespace companion::tray {

class TrayApplication {
public:
    explicit TrayApplication(core::Agent& agent);

    int run();
    std::string currentStatus() const;

private:
    void runAgentLoop();

    core::Agent& m_agent;
    mutable std::mutex m_statusMutex;
    std::string m_lastStatus{"starting"};
    bool m_exitRequested{false};
    std::thread m_workerThread;
};

}  // namespace companion::tray
