#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#endif

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace companion::adapters::windows {

#ifdef _WIN32
namespace {

constexpr unsigned short kRemoteControlPort = 5905;

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring converted(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, converted.data(), required);
    return converted;
}

std::optional<std::string> readFileText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::optional<std::uint32_t> parsePid(const std::string& body) {
    const auto keyPos = body.find("\"pid\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    const auto start = body.find_first_of("0123456789", colonPos + 1);
    if (start == std::string::npos) {
        return std::nullopt;
    }

    const auto end = body.find_first_not_of("0123456789", start);
    return static_cast<std::uint32_t>(std::stoul(body.substr(start, end - start)));
}

}  // namespace
#endif

models::RemoteAccessState WindowsRemoteAccessAdapter::currentState() const {
    return m_state;
}

bool WindowsRemoteAccessAdapter::startRemoteControl() {
#ifdef _WIN32
    if (!activeConsoleSessionAvailable()) {
        m_state.ready = false;
        m_state.active = false;
        m_state.port = kRemoteControlPort;
        m_state.failureReason = "no interactive session is active";
        return false;
    }

    if (helperIsListening()) {
        m_state.ready = true;
        m_state.active = true;
        m_state.port = kRemoteControlPort;
        m_state.failureReason.clear();
        return true;
    }

    if (!launchHelper() || !waitForHelperReady(10000)) {
        m_state.ready = false;
        m_state.active = false;
        m_state.port = kRemoteControlPort;
        if (m_state.failureReason.empty()) {
            m_state.failureReason = "remote control helper failed to start";
        }
        return false;
    }

    m_state.ready = true;
    m_state.active = true;
    m_state.port = kRemoteControlPort;
    m_state.failureReason.clear();
    return true;
#else
    m_state.ready = false;
    m_state.active = false;
    m_state.port = 0;
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::stopRemoteControl() {
#ifdef _WIN32
    if (const auto pid = helperProcessId(); pid.has_value()) {
        HANDLE processHandle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, *pid);
        if (processHandle != nullptr) {
            TerminateProcess(processHandle, 0);
            WaitForSingleObject(processHandle, 3000);
            CloseHandle(processHandle);
        }
    }

    std::error_code errorCode;
    std::filesystem::remove(helperStatePath(), errorCode);

    m_state.ready = false;
    m_state.active = false;
    m_state.port = kRemoteControlPort;
    m_state.failureReason.clear();
    return true;
#else
    m_state.ready = false;
    m_state.active = false;
    m_state.port = 0;
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::verifyReadiness() {
#ifdef _WIN32
    m_state.port = kRemoteControlPort;

    if (!activeConsoleSessionAvailable()) {
        m_state.ready = false;
        m_state.active = false;
        m_state.failureReason = "no interactive session is active";
        return false;
    }

    const bool active = helperIsListening();
    m_state.active = active;
    m_state.ready = true;
    if (!active) {
        m_state.failureReason.clear();
    } else {
        m_state.failureReason.clear();
    }
    return true;
#else
    m_state.ready = false;
    m_state.active = false;
    m_state.port = 0;
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::helperIsListening() const {
    return isTcpPortListening(kRemoteControlPort);
}

bool WindowsRemoteAccessAdapter::activeConsoleSessionAvailable() const {
#ifdef _WIN32
    const DWORD sessionId = WTSGetActiveConsoleSessionId();
    return sessionId != 0xFFFFFFFF;
#else
    return false;
#endif
}

bool WindowsRemoteAccessAdapter::launchHelper() {
#ifdef _WIN32
    const auto helperPath = helperBinaryPath();
    const auto statePath = helperStatePath();
    if (helperPath.empty()) {
        m_state.failureReason = "remote helper binary is missing";
        return false;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(std::filesystem::path(statePath).parent_path(), errorCode);

    const DWORD activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId == 0xFFFFFFFF) {
        m_state.failureReason = "no active console session";
        return false;
    }

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(activeSessionId, &userToken)) {
        m_state.failureReason = "failed to query active console token";
        return false;
    }

    HANDLE primaryToken = nullptr;
    if (!DuplicateTokenEx(userToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primaryToken)) {
        CloseHandle(userToken);
        m_state.failureReason = "failed to duplicate active console token";
        return false;
    }

    void* environment = nullptr;
    CreateEnvironmentBlock(&environment, primaryToken, FALSE);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION processInformation{};

    std::wstring commandLine = L"\"";
    commandLine += helperPath;
    commandLine += L"\" --remote-helper --port ";
    commandLine += std::to_wstring(kRemoteControlPort);
    commandLine += L" --state-file \"";
    commandLine += statePath;
    commandLine += L"\"";

    const BOOL created = CreateProcessAsUserW(
        primaryToken,
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (environment != nullptr) {
        DestroyEnvironmentBlock(environment);
    }

    CloseHandle(primaryToken);
    CloseHandle(userToken);

    if (!created) {
        m_state.failureReason = "failed to launch remote helper";
        return false;
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return true;
#else
    m_state.failureReason = "unsupported platform";
    return false;
#endif
}

std::wstring WindowsRemoteAccessAdapter::helperBinaryPath() const {
#ifdef _WIN32
    std::wstring path(4096, L'\0');
    const DWORD copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (copied == 0) {
        return {};
    }

    path.resize(copied);
    const std::wstring serviceNeedle = L"air_companion_service.exe";
    const std::wstring trayNeedle = L"air_companion_tray.exe";

    if (const auto position = path.rfind(serviceNeedle); position != std::wstring::npos) {
        path.replace(position, serviceNeedle.size(), trayNeedle);
    }

    if (!std::filesystem::exists(path)) {
        return {};
    }

    return path;
#else
    return {};
#endif
}

std::wstring WindowsRemoteAccessAdapter::helperStatePath() const {
#ifdef _WIN32
    wchar_t* programData = nullptr;
    std::wstring base = L"C:\\ProgramData";
    if (size_t length = 0; _wdupenv_s(&programData, &length, L"PROGRAMDATA") == 0 && programData != nullptr) {
        base.assign(programData);
        free(programData);
    }

    std::filesystem::path path(base);
    path /= "AIRCompanion";
    path /= "Internal";
    path /= "remote-control-helper.json";
    return path.wstring();
#else
    return {};
#endif
}

std::optional<std::uint32_t> WindowsRemoteAccessAdapter::helperProcessId() const {
#ifdef _WIN32
    const auto body = readFileText(helperStatePath());
    if (!body.has_value()) {
        return std::nullopt;
    }

    const auto pid = parsePid(*body);
    if (!pid.has_value() || !processExists(*pid)) {
        return std::nullopt;
    }

    return pid;
#else
    return std::nullopt;
#endif
}

bool WindowsRemoteAccessAdapter::processExists(std::uint32_t processId) {
#ifdef _WIN32
    HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (processHandle == nullptr) {
        return false;
    }

    const DWORD wait = WaitForSingleObject(processHandle, 0);
    CloseHandle(processHandle);
    return wait == WAIT_TIMEOUT;
#else
    (void) processId;
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

bool WindowsRemoteAccessAdapter::waitForHelperReady(int timeoutMilliseconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (helperIsListening()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

bool WindowsRemoteAccessAdapter::runCommand(const std::string& command) {
#ifdef _WIN32
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

}  // namespace companion::adapters::windows
