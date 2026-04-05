#include "FakePushUpCounterAdapter.h"

#include <chrono>
#include <thread>

namespace companion::test {

namespace {

std::string trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
        line.pop_back();
    }
    while (!line.empty() && line.front() == ' ') {
        line.erase(line.begin());
    }
    return line;
}

}  // namespace

FakePushUpCounterAdapter::FakePushUpCounterAdapter() = default;

void FakePushUpCounterAdapter::tick() {
    // In non-pumping mode, processIncoming does nothing for the fake
    // Real serial reading would happen here on Windows
}

const adapters::PushUpCounterState& FakePushUpCounterAdapter::state() const {
    return m_state;
}

bool FakePushUpCounterAdapter::startSession(
    const std::string& sessionId,
    int totalReps,
    int /*dropThreshold*/,
    int /*upGap*/,
    int /*downTolerance*/
) {
    if (!m_state.connected || !m_state.firmwareReady) {
        return false;
    }

    m_currentSessionId = sessionId;
    m_totalReps = totalReps;
    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "ready";

    if (m_testMode) {
        enqueueTestResponse(sessionId, totalReps);
    }
    return true;
}

bool FakePushUpCounterAdapter::abortSession(const std::string& /*sessionId*/) {
    if (!m_state.connected) {
        return false;
    }

    m_currentSessionId.clear();
    m_state.currentRep = 0;
    m_state.currentSet = 1;
    m_state.completionPending = false;
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "idle";
    m_responseQueue = {};
    return true;
}

bool FakePushUpCounterAdapter::consumeCompletion() {
    if (!m_state.completionPending) {
        return false;
    }
    m_state.completionPending = false;
    return true;
}

void FakePushUpCounterAdapter::processLine(const std::string& rawLine) {
    auto line = trimLine(rawLine);
    if (line.empty()) {
        return;
    }

    if (line.rfind("HELLO", 0) == 0 || line == "PONG") {
        m_state.firmwareReady = true;
        m_state.status = "ready";
        return;
    }

    if (line.rfind("DIST ", 0) == 0) {
        m_state.distance = std::atoi(line.substr(5).c_str());
        return;
    }

    if (line.rfind("REP ", 0) == 0) {
        m_state.currentRep = std::atoi(line.substr(4).c_str());
        return;
    }

    if (line.rfind("SET ", 0) == 0) {
        const auto firstSpace = line.find(' ');
        const auto secondSpace = line.find(' ', firstSpace + 1);
        const auto thirdSpace = line.find(' ', secondSpace + 1);
        if (thirdSpace != std::string::npos) {
            m_state.currentSet = std::atoi(line.substr(firstSpace + 1, secondSpace - firstSpace - 1).c_str());
        }
        return;
    }

    if (line == "STATE SEARCHING_BACK") {
        m_state.searchingBack = true;
        m_state.working = false;
        m_state.status = "searching_back";
        return;
    }

    if (line == "STATE WORK") {
        m_state.searchingBack = false;
        m_state.working = true;
        m_state.status = "working";
        return;
    }

    if (line == "STATE COMPLETE") {
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "complete";
        m_state.completionPending = true;
        return;
    }

    if (line == "STATE IDLE") {
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.status = "idle";
        return;
    }
}

void FakePushUpCounterAdapter::parseLine(const std::string& line) {
    // Same as processLine - kept for clarity
    processLine(line);
}

void FakePushUpCounterAdapter::enqueueTestResponse(const std::string& sessionId, int totalReps) {
    (void)sessionId;
    m_responseQueue = {};

    // SET 1 <target_reps> <remaining_reps>
    m_responseQueue.push("SET 1 " + std::to_string(totalReps) + " " + std::to_string(totalReps));
    m_responseQueue.push("STATE SEARCHING_BACK");

    // In test mode, firmware immediately sets hasBackCalibration=true and transitions to WORK
    // without waiting for back calibration. We simulate that here.
    m_responseQueue.push("STATE WORK");

    // Simulate each rep being reported by the firmware
    for (int rep = 1; rep <= totalReps; ++rep) {
        m_responseQueue.push("REP " + std::to_string(rep));
    }

    m_responseQueue.push("STATE COMPLETE");
}

void FakePushUpCounterAdapter::pumpIncomingFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    m_pumping = true;

    while (std::chrono::steady_clock::now() < deadline) {
        // Dequeue and process one line per call
        if (!m_responseQueue.empty()) {
            auto line = m_responseQueue.front();
            m_responseQueue.pop();
            parseLine(line);
        }

        if (m_state.completionPending) {
            break;
        }

        if (!m_testMode && (m_state.working || m_state.searchingBack || m_state.currentRep > 0)) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    m_pumping = false;
}

void FakePushUpCounterAdapter::setConnected(bool connected) {
    m_state.connected = connected;
    if (!connected) {
        m_state.firmwareReady = false;
        m_state.status = "disconnected";
        m_state.searchingBack = false;
        m_state.working = false;
        m_state.currentRep = 0;
        m_state.currentSet = 1;
        m_state.completionPending = false;
        m_currentSessionId.clear();
        m_responseQueue = {};
    }
}

void FakePushUpCounterAdapter::setFirmwareReady(bool ready) {
    m_state.firmwareReady = ready;
    if (ready && m_state.connected) {
        m_state.status = m_currentSessionId.empty() ? "ready" : m_state.status;
    }
}

void FakePushUpCounterAdapter::setTestMode(bool testMode) {
    m_testMode = testMode;
}

void FakePushUpCounterAdapter::setPortName(const std::string& port) {
    m_state.portName = port;
}

void FakePushUpCounterAdapter::simulateRepIncrement(int rep, int set) {
    m_state.currentRep = rep;
    m_state.currentSet = set;
    if (rep > 0) {
        m_state.searchingBack = false;
        m_state.working = true;
        m_state.status = "working";
    }
    if (rep >= m_totalReps && m_totalReps > 0) {
        simulateCompletion();
    }
}

void FakePushUpCounterAdapter::simulateCompletion() {
    m_state.searchingBack = false;
    m_state.working = false;
    m_state.status = "complete";
    m_state.completionPending = true;
}

void FakePushUpCounterAdapter::simulateDisconnection() {
    setConnected(false);
}

}  // namespace companion::test
