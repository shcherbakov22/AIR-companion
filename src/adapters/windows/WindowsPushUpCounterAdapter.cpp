#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace companion::adapters::windows {

namespace {
constexpr int kBaudRate = 115200;
constexpr auto kScanInterval = std::chrono::seconds(5);
constexpr auto kProbeTimeout = std::chrono::milliseconds(2200);
constexpr auto kLaunchPumpDuration = std::chrono::milliseconds(2500);

bool pushUpTestModeEnabled() {
    const char* value = std::getenv("AIR_PUSHUP_COUNTER_TEST_MODE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

void appendDebugLog(const std::string& line) {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr || *appData == '\0') {
        return;
    }

    const auto logDirectory = std::filesystem::path(appData) / "AIRCompanion";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::ofstream output(logDirectory / "debug.log", std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << line << '\n';
#else
    (void) line;
#endif
}

#ifdef _WIN32
HANDLE invalidHandle() {
    return INVALID_HANDLE_VALUE;
}

std::string trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
    }

    while (!line.empty() && line.front() == ' ') {
        line.erase(line.begin());
    }

    return line;
}

std::string portPath(const std::string& port) {
    return "\\\\.\\" + port;
}

bool configureSerialPort(HANDLE handle) {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        return false;
    }

    dcb.BaudRate = kBaudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle, &dcb)) {
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(handle, &timeouts)) {
        return false;
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

bool readChunk(HANDLE handle, std::string& buffer, std::chrono::steady_clock::time_point& lastComm) {
    char chunk[256]{};
    DWORD bytesRead = 0;
    if (!ReadFile(handle, chunk, static_cast<DWORD>(sizeof(chunk)), &bytesRead, nullptr)) {
        return false;
    }

    if (bytesRead > 0) {
        buffer.append(chunk, chunk + bytesRead);
        lastComm = std::chrono::steady_clock::now();
    }

    return true;
}

bool isFirmwareFrame(const std::string& line) {
    return line.rfind("HELLO", 0) == 0
        || line.rfind("PONG", 0) == 0
        || line.rfind("STATE ", 0) == 0
        || line.rfind("REP ", 0) == 0
        || line.rfind("SET ", 0) == 0
        || line.rfind("DIST ", 0) == 0;
}
#endif

}  // namespace

void WindowsPushUpCounterAdapter::tick() {
#ifdef _WIN32
    if (!connectIfNeeded()) {
        return;
    }

    processIncoming();
    checkInactivityReset();
#endif
}

const PushUpCounterState& WindowsPushUpCounterAdapter::state() const {
    return m_state;
}

bool WindowsPushUpCounterAdapter::startSession(const std::string& sessionId, int totalReps, int dropThreshold, int upGap, int downTolerance) {
#ifdef _WIN32
    if (!connectIfNeeded()) {
        return false;
    }

    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    const std::string command = pushUpTestModeEnabled()
        ? ("TEST " + sessionId + " " + std::to_string(totalReps) + " 80")
        : ("START " + sessionId + " " + std::to_string(totalReps) + " " + std::to_string(dropThreshold) + " " + std::to_string(upGap) + " " + std::to_string(downTolerance));
    appendDebugLog("push-up counter send: " + command);
    if (!sendLine(command)) {
        return false;
    }

    pumpIncomingFor(kLaunchPumpDuration);
    return true;
#else
    (void) sessionId;
    (void) totalReps;
    (void) dropThreshold;
    (void) upGap;
    (void) downTolerance;
    return false;
#endif
}

bool WindowsPushUpCounterAdapter::abortSession(const std::string& sessionId) {
#ifdef _WIN32
    if (!connectIfNeeded()) {
        return false;
    }

    return sendLine("ABORT " + sessionId);
#else
    (void) sessionId;
    return false;
#endif
}

bool WindowsPushUpCounterAdapter::consumeCompletion() {
    if (!m_state.completionPending) {
        return false;
    }

    m_state.completionPending = false;
    return true;
}

bool WindowsPushUpCounterAdapter::connectIfNeeded() {
#ifdef _WIN32
    if (m_handle != nullptr && static_cast<HANDLE>(m_handle) != invalidHandle()) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_lastScanAt.time_since_epoch().count() != 0 && (now - m_lastScanAt) < kScanInterval) {
        return false;
    }
    m_lastScanAt = now;

    for (int index = 1; index <= 32; ++index) {
        const std::string port = "COM" + std::to_string(index);
        HANDLE candidate = CreateFileA(
            portPath(port).c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (candidate == invalidHandle()) {
            continue;
        }

        if (!configureSerialPort(candidate)) {
            CloseHandle(candidate);
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        const std::string ping = "PING\n";
        DWORD bytesWritten = 0;
        WriteFile(candidate, ping.data(), static_cast<DWORD>(ping.size()), &bytesWritten, nullptr);

        std::string probeBuffer;
        const auto deadline = std::chrono::steady_clock::now() + kProbeTimeout;
        bool matched = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!readChunk(candidate, probeBuffer, m_lastCommunicationAt)) {
                break;
            }

            std::size_t newline = std::string::npos;
            while ((newline = probeBuffer.find('\n')) != std::string::npos) {
                auto line = trimLine(probeBuffer.substr(0, newline));
                probeBuffer.erase(0, newline + 1);
                if (isFirmwareFrame(line)) {
                    matched = true;
                    break;
                }
            }

            if (matched) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }

        if (!matched) {
            CloseHandle(candidate);
            continue;
        }

        m_handle = candidate;
        m_state.connected = true;
        m_state.portName = port;
        m_state.status = "connected";
        m_state.firmwareReady = true;
        m_buffer.clear();
        appendDebugLog("push-up counter connected on " + port);
        return true;
    }

    m_state.connected = false;
    m_state.firmwareReady = false;
    m_state.status = "disconnected";
    m_state.portName.clear();
    return false;
#else
    return false;
#endif
}

void WindowsPushUpCounterAdapter::disconnect() {
#ifdef _WIN32
    if (m_handle != nullptr && static_cast<HANDLE>(m_handle) != invalidHandle()) {
        CloseHandle(static_cast<HANDLE>(m_handle));
    }
#endif
    m_handle = nullptr;
    m_state.connected = false;
    m_state.firmwareReady = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "disconnected";
    m_state.portName.clear();
    m_buffer.clear();
    m_lastCommunicationAt = {};
}

void WindowsPushUpCounterAdapter::processIncoming() {
#ifdef _WIN32
    if (m_handle == nullptr || static_cast<HANDLE>(m_handle) == invalidHandle()) {
        return;
    }

    if (!readChunk(static_cast<HANDLE>(m_handle), m_buffer, m_lastCommunicationAt)) {
        appendDebugLog("push-up counter read failed, disconnecting");
        disconnect();
        return;
    }

    std::size_t newline = std::string::npos;
    while ((newline = m_buffer.find('\n')) != std::string::npos) {
        auto line = trimLine(m_buffer.substr(0, newline));
        m_buffer.erase(0, newline + 1);
        parseLine(line);
    }
#endif
}

void WindowsPushUpCounterAdapter::processLine(const std::string& rawLine) {
#ifdef _WIN32
    parseLine(trimLine(rawLine));
#else
    (void)rawLine;
#endif
}

void WindowsPushUpCounterAdapter::parseLine(const std::string& line) {
#ifdef _WIN32
    if (line.empty()) {
        return;
    }

    if (line.rfind("HELLO", 0) == 0 || line == "PONG") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.firmwareReady = true;
        m_state.status = "ready";
        return;
    }

    if (line.rfind("DIST ", 0) == 0) {
        m_state.distance = std::atoi(line.substr(5).c_str());
        return;
    }

    if (line.rfind("REP ", 0) == 0) {
        appendDebugLog("push-up counter recv: " + line);
        m_state.currentRep = std::atoi(line.substr(4).c_str());
        return;
    }

    if (line.rfind("SET ", 0) == 0) {
        appendDebugLog("push-up counter recv: " + line);
        const auto firstSpace = line.find(' ');
        const auto secondSpace = line.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos) {
            m_state.currentSet = std::atoi(line.substr(firstSpace + 1, secondSpace - firstSpace - 1).c_str());
        }
        return;
    }

    if (line == "STATE SEARCHING_BACK") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = true;
        m_state.working = false;
        m_state.status = "searching_back";
        return;
    }

    if (line == "STATE WORK") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = true;
        m_state.status = "working";
        return;
    }

    if (line == "STATE COMPLETE") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "complete";
        m_state.completionPending = true;
        return;
    }

    if (line == "STATE IDLE") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "idle";
        return;
    }
#endif
}

void WindowsPushUpCounterAdapter::pumpIncomingFor(std::chrono::milliseconds duration) {
#ifdef _WIN32
    const bool testMode = pushUpTestModeEnabled();
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        processIncoming();
        if (m_state.completionPending) {
            break;
        }

        if (!testMode && (m_state.working || m_state.searchingBack || m_state.currentRep > 0)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
#else
    (void) duration;
#endif
}

bool WindowsPushUpCounterAdapter::sendLine(const std::string& line) {
#ifdef _WIN32
    if (m_handle == nullptr || static_cast<HANDLE>(m_handle) == invalidHandle()) {
        return false;
    }

    std::string payload = line + "\n";
    DWORD bytesWritten = 0;
    const auto ok = WriteFile(static_cast<HANDLE>(m_handle), payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten, nullptr) != 0;
    if (!ok) {
        appendDebugLog("push-up counter write failed");
        disconnect();
        return false;
    }

    if (bytesWritten == payload.size()) {
        m_lastCommunicationAt = std::chrono::steady_clock::now();
    }
    return bytesWritten == payload.size();
#else
    (void) line;
    return false;
#endif
}

void WindowsPushUpCounterAdapter::checkInactivityReset() {
#ifdef _WIN32
    if (m_handle == nullptr || static_cast<HANDLE>(m_handle) == invalidHandle()) {
        return;
    }

    if (m_lastCommunicationAt.time_since_epoch().count() == 0) {
        return;
    }

    if ((std::chrono::steady_clock::now() - m_lastCommunicationAt) >= kInactivityTimeout) {
        appendDebugLog("push-up counter inactivity timeout, resetting connection");
        disconnect();
    }
#endif
}

void WindowsPushUpCounterAdapter::resetAfterInactivity() {
#ifdef _WIN32
    appendDebugLog("push-up counter manual inactivity reset");
    disconnect();
#endif
}

}  // namespace companion::adapters::windows
