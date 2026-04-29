#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace companion::adapters::windows {

namespace {
constexpr int kBaudRate = 9600;
constexpr auto kScanInterval = std::chrono::seconds(5);
constexpr auto kProbeTimeout = std::chrono::milliseconds(8500);
constexpr auto kLaunchPumpDuration = std::chrono::milliseconds(2500);
constexpr int kMaxLoggedOpenFailuresPerScan = 12;

bool pushUpTestModeEnabled() {
    const char* value = std::getenv("AIR_PUSHUP_COUNTER_TEST_MODE");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

std::filesystem::path debugLogPath() {
#ifdef _WIN32
    if (const char* programData = std::getenv("ProgramData"); programData != nullptr && *programData != '\0') {
        return std::filesystem::path(programData) / "AIRCompanion" / "Logs" / "debug.log";
    }
#endif
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "AIRCompanion" / "debug.log";
    }
    return std::filesystem::path(".") / "AIRCompanion" / "Logs" / "debug.log";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return buffer;
}

void appendDebugLog(const std::string& line) {
#ifdef _WIN32
    const auto logPath = debugLogPath();
    std::error_code errorCode;
    std::filesystem::create_directories(logPath.parent_path(), errorCode);

    std::ofstream output(logPath, std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << timestamp() << " " << line << '\n';
#else
    (void) line;
#endif
}

std::string stateSummary(const PushUpCounterState& state) {
    std::ostringstream output;
    output << "connected=" << (state.connected ? "1" : "0")
           << " firmwareReady=" << (state.firmwareReady ? "1" : "0")
           << " port=" << (state.portName.empty() ? "-" : state.portName)
           << " status=" << state.status
           << " distance=" << state.distance
           << " rep=" << state.currentRep
           << " set=" << state.currentSet
           << " searchingBack=" << (state.searchingBack ? "1" : "0")
           << " working=" << (state.working ? "1" : "0")
           << " completionPending=" << (state.completionPending ? "1" : "0");
    if (!state.errorMessage.empty()) {
        output << " error=" << state.errorMessage;
    }
    return output.str();
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

int readChunk(HANDLE handle, std::string& buffer, std::chrono::steady_clock::time_point& lastComm) {
    char chunk[256]{};
    DWORD bytesRead = 0;
    if (!ReadFile(handle, chunk, static_cast<DWORD>(sizeof(chunk)), &bytesRead, nullptr)) {
        return -1;
    }

    if (bytesRead > 0) {
        buffer.append(chunk, chunk + bytesRead);
        lastComm = std::chrono::steady_clock::now();
    }

    return static_cast<int>(bytesRead);
}

bool isFirmwareFrame(const std::string& line) {
    return line.rfind("HELLO", 0) == 0
        || line.rfind("PONG", 0) == 0
        || line.rfind("STATE ", 0) == 0
        || line.rfind("REP ", 0) == 0
        || line.rfind("SET ", 0) == 0
        || line.rfind("DIST ", 0) == 0;
}

int comPortNumber(const std::string& port) {
    if (port.size() <= 3 || port.rfind("COM", 0) != 0) {
        return 0;
    }

    for (std::size_t index = 3; index < port.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(port[index]))) {
            return 0;
        }
    }

    return std::atoi(port.substr(3).c_str());
}

std::vector<std::string> enumerateComPorts() {
    std::vector<std::string> ports;
    std::vector<char> buffer(32768, '\0');
    const DWORD length = QueryDosDeviceA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0) {
        const char* cursor = buffer.data();
        while (*cursor != '\0') {
            std::string name(cursor);
            if (comPortNumber(name) > 0) {
                ports.push_back(std::move(name));
            }
            cursor += name.size() + 1;
        }
    }

    if (ports.empty()) {
        for (int index = 1; index <= 256; ++index) {
            ports.push_back("COM" + std::to_string(index));
        }
    }

    std::sort(ports.begin(), ports.end(), [](const std::string& left, const std::string& right) {
        return comPortNumber(left) < comPortNumber(right);
    });
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}
#endif

}  // namespace

void WindowsPushUpCounterAdapter::tick() {
#ifdef _WIN32
    if (!connectIfNeeded()) {
        appendDebugLog("push-up counter tick: not connected state=" + stateSummary(m_state));
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
        appendDebugLog("push-up counter start rejected: no serial connection session=" + sessionId);
        return false;
    }

    appendDebugLog("push-up counter start prepare: session=" + sessionId
        + " totalReps=" + std::to_string(totalReps)
        + " drop=" + std::to_string(dropThreshold)
        + " upGap=" + std::to_string(upGap)
        + " downTolerance=" + std::to_string(downTolerance)
        + " stateBefore=" + stateSummary(m_state));
    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "ready";
    m_state.errorMessage.clear();
    const std::string command = pushUpTestModeEnabled()
        ? ("TEST " + sessionId + " " + std::to_string(totalReps) + " 80")
        : ("START " + sessionId + " " + std::to_string(totalReps) + " " + std::to_string(dropThreshold) + " " + std::to_string(upGap) + " " + std::to_string(downTolerance));
    appendDebugLog("push-up counter send: " + command);
    if (!sendLine(command)) {
        appendDebugLog("push-up counter start send failed: session=" + sessionId);
        return false;
    }

    pumpIncomingFor(kLaunchPumpDuration);
    const bool started = m_state.searchingBack
        || m_state.working
        || m_state.currentRep > 0
        || m_state.completionPending
        || m_state.status == "complete";
    appendDebugLog("push-up counter start result: session=" + sessionId
        + " started=" + std::string(started ? "1" : "0")
        + " stateAfter=" + stateSummary(m_state));
    return started;
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
        appendDebugLog("push-up counter abort rejected: no serial connection session=" + sessionId);
        return false;
    }

    appendDebugLog("push-up counter abort send: session=" + sessionId + " stateBefore=" + stateSummary(m_state));
    const bool ok = sendLine("ABORT " + sessionId);
    appendDebugLog("push-up counter abort result: session=" + sessionId + " ok=" + std::string(ok ? "1" : "0"));
    return ok;
#else
    (void) sessionId;
    return false;
#endif
}

void WindowsPushUpCounterAdapter::hardReset() {
#ifdef _WIN32
    hardResetArduino();
#endif
}

bool WindowsPushUpCounterAdapter::consumeCompletion() {
    if (!m_state.completionPending) {
        appendDebugLog("push-up counter consumeCompletion ignored: no pending completion state=" + stateSummary(m_state));
        return false;
    }

    m_state.completionPending = false;
    appendDebugLog("push-up counter completion consumed state=" + stateSummary(m_state));
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

    const auto ports = enumerateComPorts();
    appendDebugLog("push-up counter scan start ports=" + std::to_string(ports.size()));
    int loggedOpenFailures = 0;
    for (const std::string& port : ports) {
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
            if (loggedOpenFailures < kMaxLoggedOpenFailuresPerScan) {
                appendDebugLog("push-up counter open failed on " + port + " error=" + std::to_string(GetLastError()));
                ++loggedOpenFailures;
            }
            continue;
        }

        if (!configureSerialPort(candidate)) {
            appendDebugLog("push-up counter probe configure failed on " + port + " error=" + std::to_string(GetLastError()));
            CloseHandle(candidate);
            continue;
        }

        appendDebugLog("push-up counter probing " + port);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const std::string ping = "PING\n";
        DWORD bytesWritten = 0;
        if (!WriteFile(candidate, ping.data(), static_cast<DWORD>(ping.size()), &bytesWritten, nullptr)) {
            appendDebugLog("push-up counter probe PING write failed on " + port + " error=" + std::to_string(GetLastError()));
            CloseHandle(candidate);
            continue;
        }
        appendDebugLog("push-up counter probe PING wrote bytes=" + std::to_string(bytesWritten) + " port=" + port);

        std::string probeBuffer;
        const auto deadline = std::chrono::steady_clock::now() + kProbeTimeout;
        bool matched = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (readChunk(candidate, probeBuffer, m_lastCommunicationAt) < 0) {
                appendDebugLog("push-up counter probe read failed on " + port + " error=" + std::to_string(GetLastError()));
                break;
            }

            std::size_t newline = std::string::npos;
            while ((newline = probeBuffer.find('\n')) != std::string::npos) {
                auto line = trimLine(probeBuffer.substr(0, newline));
                probeBuffer.erase(0, newline + 1);
                appendDebugLog("push-up counter probe recv on " + port + ": " + line);
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
            appendDebugLog("push-up counter probe no firmware response on " + port);
            CloseHandle(candidate);
            continue;
        }

        m_handle = candidate;
        m_state.connected = true;
        m_state.portName = port;
        m_state.status = "connected";
        m_state.firmwareReady = true;
        m_state.errorMessage.clear();
        m_buffer.clear();
        appendDebugLog("push-up counter connected on " + port + " state=" + stateSummary(m_state));
        return true;
    }

    if (loggedOpenFailures >= kMaxLoggedOpenFailuresPerScan) {
        appendDebugLog("push-up counter open failures truncated after " + std::to_string(kMaxLoggedOpenFailuresPerScan) + " ports");
    }
    m_state.connected = false;
    m_state.firmwareReady = false;
    m_state.status = "disconnected";
    m_state.portName.clear();
    m_state.errorMessage.clear();
    appendDebugLog("push-up counter scan end: no firmware found");
    return false;
#else
    return false;
#endif
}

void WindowsPushUpCounterAdapter::disconnect() {
#ifdef _WIN32
    appendDebugLog("push-up counter disconnect stateBefore=" + stateSummary(m_state));
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
    m_state.errorMessage.clear();
    m_buffer.clear();
    m_lastCommunicationAt = {};
}

void WindowsPushUpCounterAdapter::hardResetArduino() {
#ifdef _WIN32
    if (m_handle == nullptr || static_cast<HANDLE>(m_handle) == invalidHandle()) {
        return;
    }

    appendDebugLog("push-up counter hard reset via DTR stateBefore=" + stateSummary(m_state));
    EscapeCommFunction(static_cast<HANDLE>(m_handle), CLRDTR);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EscapeCommFunction(static_cast<HANDLE>(m_handle), SETDTR);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    disconnect();
#endif
}

void WindowsPushUpCounterAdapter::processIncoming() {
#ifdef _WIN32
    if (m_handle == nullptr || static_cast<HANDLE>(m_handle) == invalidHandle()) {
        return;
    }

    // The firmware emits DIST telemetry every 50ms. Reading a single small
    // chunk per service tick can leave completion frames stuck behind telemetry.
    for (int reads = 0; reads < 64; ++reads) {
        const int bytesRead = readChunk(static_cast<HANDLE>(m_handle), m_buffer, m_lastCommunicationAt);
        if (bytesRead < 0) {
            appendDebugLog("push-up counter read failed, disconnecting error=" + std::to_string(GetLastError()));
            disconnect();
            return;
        }

        std::size_t newline = std::string::npos;
        while ((newline = m_buffer.find('\n')) != std::string::npos) {
            auto line = trimLine(m_buffer.substr(0, newline));
            m_buffer.erase(0, newline + 1);
            parseLine(line);
        }

        if (bytesRead == 0) {
            break;
        }
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
        m_state.errorMessage.clear();
        return;
    }

    if (line.rfind("DIST ", 0) == 0) {
        m_state.distance = std::atoi(line.substr(5).c_str());
        static int distanceLogCounter = 0;
        ++distanceLogCounter;
        if (distanceLogCounter == 1 || distanceLogCounter >= 20) {
            appendDebugLog("push-up counter recv telemetry: " + line + " state=" + stateSummary(m_state));
            distanceLogCounter = 0;
        }
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
        m_state.errorMessage.clear();
        return;
    }

    if (line == "STATE WORK") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = true;
        m_state.status = "working";
        m_state.errorMessage.clear();
        return;
    }

    if (line == "STATE COMPLETE") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "complete";
        m_state.completionPending = true;
        m_state.errorMessage.clear();
        return;
    }

    if (line == "STATE IDLE") {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "idle";
        m_state.errorMessage.clear();
        return;
    }

    if (line.rfind("STATE ERROR ", 0) == 0) {
        appendDebugLog("push-up counter recv: " + line);
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "error";
        m_state.errorMessage = line.substr(std::string("STATE ERROR ").size());
        return;
    }

    appendDebugLog("push-up counter recv unrecognized: " + line);
#endif
}

void WindowsPushUpCounterAdapter::pumpIncomingFor(std::chrono::milliseconds duration) {
#ifdef _WIN32
    const bool testMode = pushUpTestModeEnabled();
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        processIncoming();
        if (m_state.completionPending) {
            appendDebugLog("push-up counter pump stopped: completion pending state=" + stateSummary(m_state));
            break;
        }

        if (!testMode && (m_state.working || m_state.searchingBack || m_state.currentRep > 0)) {
            appendDebugLog("push-up counter pump stopped: start acknowledged state=" + stateSummary(m_state));
            break;
        }

        if (m_state.status == "error") {
            appendDebugLog("push-up counter pump stopped: firmware error state=" + stateSummary(m_state));
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
        appendDebugLog("push-up counter write failed error=" + std::to_string(GetLastError()) + " line=" + line);
        disconnect();
        return false;
    }

    if (bytesWritten == payload.size()) {
        m_lastCommunicationAt = std::chrono::steady_clock::now();
    } else {
        appendDebugLog("push-up counter short write line=" + line
            + " expected=" + std::to_string(payload.size())
            + " actual=" + std::to_string(bytesWritten));
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
        appendDebugLog("push-up counter inactivity timeout, resetting connection state=" + stateSummary(m_state));
        disconnect();
    }
#endif
}

void WindowsPushUpCounterAdapter::resetAfterInactivity() {
#ifdef _WIN32
    appendDebugLog("push-up counter manual inactivity reset");
    hardResetArduino();
#endif
}

}  // namespace companion::adapters::windows
