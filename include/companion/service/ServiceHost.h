#pragma once

#include <functional>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace companion::service {

class ServiceHost {
public:
    using Worker = std::function<void(ServiceHost&)>;

    explicit ServiceHost(Worker worker);

    int run();
    const std::string& lastStatus() const;
    bool stopRequested() const;
    bool consumeResumeRequested();
    void setLastStatus(std::string status);
    bool waitForStop(unsigned long milliseconds);

private:
    int runConsoleLoop();
    void serviceMain();
    unsigned long controlHandler(unsigned long control, unsigned long eventType, void* eventData);
    void workerLoop();
    void reportStatus(unsigned long currentState, unsigned long win32ExitCode = 0, unsigned long waitHint = 0);

#ifdef _WIN32
    static void WINAPI serviceMainThunk(unsigned long argc, wchar_t** argv);
    static unsigned long WINAPI controlHandlerThunk(unsigned long control, unsigned long eventType, void* eventData, void* context);
#endif

    Worker m_worker;
    std::string m_lastStatus{"idle"};
    bool m_stopRequested{false};
    bool m_resumeRequested{false};
    std::thread m_workerThread;

#ifdef _WIN32
    SERVICE_STATUS_HANDLE m_statusHandle{nullptr};
    SERVICE_STATUS m_serviceStatus{};
    HANDLE m_stopEvent{nullptr};
    static ServiceHost* s_instance;
#endif
};

}  // namespace companion::service
