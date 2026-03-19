#include "companion/service/ServiceHost.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace companion::service {

namespace {

#ifdef _WIN32
constexpr wchar_t kServiceName[] = L"AIRCompanion";
#endif

}  // namespace

#ifdef _WIN32
ServiceHost* ServiceHost::s_instance = nullptr;
#endif

ServiceHost::ServiceHost(core::Agent& agent) : m_agent(agent) {}

int ServiceHost::run() {
#ifdef _WIN32
    s_instance = this;

    SERVICE_TABLE_ENTRYW serviceTable[] = {
        {const_cast<LPWSTR>(kServiceName), &ServiceHost::serviceMainThunk},
        {nullptr, nullptr},
    };

    if (StartServiceCtrlDispatcherW(serviceTable) != FALSE) {
        return 0;
    }

    if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        return runConsoleLoop();
    }

    return 1;
#else
    return runConsoleLoop();
#endif
}

const std::string& ServiceHost::lastStatus() const {
    return m_lastStatus;
}

int ServiceHost::runConsoleLoop() {
    m_stopRequested = false;
    m_resumeRequested = false;
    m_agent.start();

    while (!m_stopRequested && m_agent.running()) {
        m_agent.tick();
        m_lastStatus = m_agent.statusSummary();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    m_agent.stop();
    return 0;
}

void ServiceHost::serviceMain() {
#ifdef _WIN32
    m_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, &ServiceHost::controlHandlerThunk, this);
    if (m_statusHandle == nullptr) {
        return;
    }

    ZeroMemory(&m_serviceStatus, sizeof(m_serviceStatus));
    m_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    m_serviceStatus.dwControlsAccepted = 0;

    reportStatus(SERVICE_START_PENDING, NO_ERROR, 5000);

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_stopEvent == nullptr) {
        reportStatus(SERVICE_STOPPED, static_cast<unsigned long>(GetLastError()));
        return;
    }

    m_stopRequested = false;
    m_resumeRequested = false;
    m_workerThread = std::thread([this] {
        workerLoop();
    });

    reportStatus(SERVICE_RUNNING, NO_ERROR, 0);

    WaitForSingleObject(m_stopEvent, INFINITE);

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (m_stopEvent != nullptr) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    reportStatus(SERVICE_STOPPED, NO_ERROR, 0);
#endif
}

unsigned long ServiceHost::controlHandler(unsigned long control, unsigned long eventType, void* /*eventData*/) {
#ifdef _WIN32
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            m_stopRequested = true;
            m_agent.stop();
            if (m_stopEvent != nullptr) {
                SetEvent(m_stopEvent);
            }
            return NO_ERROR;

        case SERVICE_CONTROL_POWEREVENT:
            if (eventType == PBT_APMRESUMEAUTOMATIC || eventType == PBT_APMRESUMESUSPEND) {
                m_resumeRequested = true;
            }
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
    }
#else
    (void) control;
    (void) eventType;
#endif

    return ERROR_CALL_NOT_IMPLEMENTED;
}

void ServiceHost::workerLoop() {
    m_agent.start();

#ifdef _WIN32
    while (!m_stopRequested && m_agent.running()) {
        m_agent.tick();
        m_lastStatus = m_agent.statusSummary();

        const auto waitMs = m_resumeRequested ? 0 : 1000;
        m_resumeRequested = false;
        if (m_stopEvent != nullptr && WaitForSingleObject(m_stopEvent, waitMs) == WAIT_OBJECT_0) {
            break;
        }
    }

    if (m_stopEvent != nullptr) {
        SetEvent(m_stopEvent);
    }
#else
    while (!m_stopRequested && m_agent.running()) {
        m_agent.tick();
        m_lastStatus = m_agent.statusSummary();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif

    m_agent.stop();
}

void ServiceHost::reportStatus(unsigned long currentState, unsigned long win32ExitCode, unsigned long waitHint) {
#ifdef _WIN32
    if (m_statusHandle == nullptr) {
        return;
    }

    m_serviceStatus.dwCurrentState = currentState;
    m_serviceStatus.dwWin32ExitCode = win32ExitCode;
    m_serviceStatus.dwWaitHint = waitHint;
    m_serviceStatus.dwControlsAccepted = currentState == SERVICE_START_PENDING
        ? 0
        : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_POWEREVENT;
    m_serviceStatus.dwCheckPoint = currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED
        ? 0
        : m_serviceStatus.dwCheckPoint + 1;

    SetServiceStatus(m_statusHandle, &m_serviceStatus);
#else
    (void) currentState;
    (void) win32ExitCode;
    (void) waitHint;
#endif
}

void WINAPI ServiceHost::serviceMainThunk(unsigned long /*argc*/, wchar_t** /*argv*/) {
#ifdef _WIN32
    if (s_instance != nullptr) {
        s_instance->serviceMain();
    }
#endif
}

unsigned long WINAPI ServiceHost::controlHandlerThunk(
    unsigned long control,
    unsigned long eventType,
    void* eventData,
    void* context
) {
#ifdef _WIN32
    auto* host = reinterpret_cast<ServiceHost*>(context);
    return host != nullptr
        ? host->controlHandler(control, eventType, eventData)
        : ERROR_CALL_NOT_IMPLEMENTED;
#else
    (void) control;
    (void) eventType;
    (void) eventData;
    (void) context;
    return 0;
#endif
}

}  // namespace companion::service
