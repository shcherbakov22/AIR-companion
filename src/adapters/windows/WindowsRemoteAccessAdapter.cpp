#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <string>

#include <windows.h>
#endif

namespace companion::adapters::windows {

models::RemoteAccessState WindowsRemoteAccessAdapter::currentState() const {
    return m_state;
}

bool WindowsRemoteAccessAdapter::ensureEnabled(const std::string& username, const std::string& password) {
    m_state.username = username;

#ifdef _WIN32
    const auto securePassword = "ConvertTo-SecureString " + quoteForPowerShell(password) + " -AsPlainText -Force";
    const bool userOk = localUserExists(username)
        ? runCommand("powershell -NoProfile -NonInteractive -Command \"Set-LocalUser -Name " + quoteForPowerShell(username) + " -Password (" + securePassword + ")\"")
        : runCommand("powershell -NoProfile -NonInteractive -Command \"New-LocalUser -Name " + quoteForPowerShell(username) + " -Password (" + securePassword + ") -AccountNeverExpires\"");
    const bool rdpOk = runCommand("reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Terminal Server\" /v fDenyTSConnections /t REG_DWORD /d 0 /f");
    const bool firewallOk = runCommand("netsh advfirewall firewall set rule group=\"remote desktop\" new enable=Yes");
    const bool groupOk = runCommand("powershell -NoProfile -NonInteractive -Command \"Add-LocalGroupMember -Group 'Remote Desktop Users' -Member " + quoteForPowerShell(username) + " -ErrorAction SilentlyContinue\"")
        || runCommand("powershell -NoProfile -NonInteractive -Command \"Add-LocalGroupMember -Group 'Administrators' -Member " + quoteForPowerShell(username) + " -ErrorAction SilentlyContinue\"");
    runCommand("sc config TermService start= auto");
    runCommand("sc config UmRdpService start= auto");
    const bool termServiceOk = ensureServiceRunning("TermService");
    const bool userModeServiceOk = ensureServiceRunning("UmRdpService");

    if (!userOk || !rdpOk || !firewallOk || !groupOk || !termServiceOk || !userModeServiceOk) {
        m_state.ready = false;
        m_state.failureReason = "failed to configure local RDP access";
        return false;
    }

    return verifyReadiness();
#else
    m_state.ready = false;
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::verifyReadiness() {
#ifdef _WIN32
    if (!isRdpEnabledInRegistry()) {
        m_state.ready = false;
        m_state.failureReason = "rdp disabled in registry";
        return false;
    }

    if (!ensureServiceRunning("TermService")) {
        m_state.ready = false;
        m_state.failureReason = "remote desktop service is not running";
        return false;
    }

    if (!ensureServiceRunning("UmRdpService")) {
        m_state.ready = false;
        m_state.failureReason = "user-mode port redirector is not running";
        return false;
    }

    if (!m_state.username.empty() && !localUserExists(m_state.username)) {
        m_state.ready = false;
        m_state.failureReason = "remote control account is missing";
        return false;
    }

    if (!isTcpPortListening(3389)) {
        m_state.ready = false;
        m_state.failureReason = "rdp listener is not listening on 3389";
        return false;
    }

    m_state.ready = true;
    m_state.failureReason.clear();
    return true;
#else
    m_state.ready = false;
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::ensureServiceRunning(const std::string& serviceName) const {
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return false;
    }

    const auto utf8ToWide = [](const std::string& value) {
        const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (required <= 1) {
            return std::wstring{};
        }

        std::wstring converted(static_cast<std::size_t>(required - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), required);
        return converted;
    };

    const std::wstring wideServiceName = utf8ToWide(serviceName);
    SC_HANDLE service = OpenServiceW(scm, wideServiceName.c_str(), SERVICE_QUERY_STATUS | SERVICE_START);
    if (service == nullptr) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    if (status.dwCurrentState == SERVICE_RUNNING) {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return true;
    }

    if (status.dwCurrentState != SERVICE_START_PENDING) {
        StartServiceW(service, 0, nullptr);
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        Sleep(500);
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
            break;
        }

        if (status.dwCurrentState == SERVICE_RUNNING) {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return true;
        }

        if (status.dwCurrentState != SERVICE_START_PENDING) {
            break;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return false;
#else
    (void) serviceName;
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::isTcpPortListening(unsigned short port) const {
#ifdef _WIN32
    return runCommand(
        "powershell -NoProfile -NonInteractive -Command \""
        "$listener = Get-NetTCPConnection -State Listen -LocalPort " + std::to_string(port) + " -ErrorAction SilentlyContinue; "
        "if ($listener) { exit 0 } else { exit 1 }"
        "\""
    );
#else
    (void) port;
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::runCommand(const std::string& command) {
#ifdef _WIN32
    const auto utf8ToWide = [](const std::string& value) {
        const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (required <= 1) {
            return std::wstring{};
        }

        std::wstring converted(static_cast<std::size_t>(required - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), required);
        return converted;
    };

    std::wstring commandLine = L"cmd.exe /C ";
    commandLine += utf8ToWide(command);
    if (commandLine.empty()) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    const auto created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInformation
    );
    if (!created) {
        return false;
    }

    const auto waitResult = WaitForSingleObject(processInformation.hProcess, 15000);
    bool ok = false;
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        ok = GetExitCodeProcess(processInformation.hProcess, &exitCode) != FALSE && exitCode == 0;
    } else {
        TerminateProcess(processInformation.hProcess, 1);
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return ok;
#else
    (void) command;
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::localUserExists(const std::string& username) {
#ifdef _WIN32
    return runCommand("net user " + quoteForCommand(username) + " >nul 2>&1");
#else
    (void) username;
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::isRdpEnabledInRegistry() {
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server",
            0,
            KEY_READ,
            &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    const auto result = RegQueryValueExW(key, L"fDenyTSConnections", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && value == 0;
#else
    return false;
#endif
}

std::string WindowsRemoteAccessAdapter::quoteForCommand(const std::string& value) {
    return "\"" + value + "\"";
}

std::string WindowsRemoteAccessAdapter::quoteForPowerShell(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

}  // namespace companion::adapters::windows
