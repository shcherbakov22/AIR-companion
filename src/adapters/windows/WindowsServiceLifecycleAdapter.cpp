#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <string>

#include <windows.h>

namespace {

constexpr wchar_t kServiceName[] = L"AIRCompanion";
constexpr wchar_t kServiceDisplayName[] = L"AIR Companion";

std::wstring currentBinaryPath() {
    std::wstring path(4096, L'\0');
    const auto copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (copied == 0) {
        return {};
    }

    path.resize(copied);
    return path;
}

}  // namespace
#endif

namespace companion::adapters::windows {

bool WindowsServiceLifecycleAdapter::install() {
#ifdef _WIN32
    const auto binaryPath = currentBinaryPath();
    if (binaryPath.empty()) {
        return false;
    }

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (manager == nullptr) {
        return false;
    }

    SC_HANDLE service = CreateServiceW(
        manager,
        kServiceName,
        kServiceDisplayName,
        SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binaryPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    if (service == nullptr && GetLastError() == ERROR_SERVICE_EXISTS) {
        service = OpenServiceW(manager, kServiceName, SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP);
        if (service != nullptr) {
            ChangeServiceConfigW(
                service,
                SERVICE_NO_CHANGE,
                SERVICE_AUTO_START,
                SERVICE_NO_CHANGE,
                binaryPath.c_str(),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                kServiceDisplayName);
        }
    }

    const bool ok = service != nullptr;
    if (service != nullptr) {
        CloseServiceHandle(service);
    }

    CloseServiceHandle(manager);
    return ok;
#else
    return false;
#endif
}

bool WindowsServiceLifecycleAdapter::start() {
#ifdef _WIN32
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const bool alreadyRunning =
        QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) != FALSE &&
        status.dwCurrentState == SERVICE_RUNNING;

    bool ok = alreadyRunning || StartServiceW(service, 0, nullptr) != FALSE || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
#else
    return false;
#endif
}

bool WindowsServiceLifecycleAdapter::stop() {
#ifdef _WIN32
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    const bool alreadyStopped =
        QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) != FALSE &&
        status.dwCurrentState == SERVICE_STOPPED;

    SERVICE_STATUS stopStatus{};
    const bool ok = alreadyStopped || ControlService(service, SERVICE_CONTROL_STOP, &stopStatus) != FALSE;

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
#else
    return false;
#endif
}

}  // namespace companion::adapters::windows

