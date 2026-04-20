#pragma once

#include "companion/adapters/IPushUpCounterAdapter.h"

#include <chrono>
#include <queue>
#include <string>
#include <vector>

namespace companion::test {

class FakePushUpCounterAdapter : public adapters::IPushUpCounterAdapter {
public:
    FakePushUpCounterAdapter();

    void tick() override;
    const adapters::PushUpCounterState& state() const override;
    bool startSession(const std::string& sessionId, int totalReps, int dropThreshold, int upGap, int downTolerance) override;
    bool abortSession(const std::string& sessionId) override;
    void hardReset() override;
    bool consumeCompletion() override;
    void processLine(const std::string& line) override;
    void resetAfterInactivity() override;

    // Test helpers
    void setConnected(bool connected);
    void setFirmwareReady(bool ready);
    void setTestMode(bool testMode);
    void setAutoStartAck(bool enabled);
    void setPortName(const std::string& port);
    void simulateRepIncrement(int rep, int set = 1);
    void simulateCompletion();
    void simulateDisconnection();
    void pumpIncomingFor(std::chrono::milliseconds duration);
    int abortCount() const;
    int hardResetCount() const;
    bool completionPending() const;

private:
    void parseLine(const std::string& line);
    void enqueueTestResponse(const std::string& sessionId, int totalReps);

    adapters::PushUpCounterState m_state;
    std::string m_currentSessionId;
    int m_totalReps{0};
    bool m_testMode{false};
    bool m_autoStartAck{true};
    bool m_pumping{false};
    int m_abortCount{0};
    int m_hardResetCount{0};
    std::queue<std::string> m_responseQueue;
    std::queue<std::string> m_sentCommands;
};

}  // namespace companion::test
