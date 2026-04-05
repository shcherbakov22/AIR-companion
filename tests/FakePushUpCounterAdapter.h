#pragma once

#include "companion/adapters/IPushUpCounterAdapter.h"

namespace companion::test {

class FakePushUpCounterAdapter : public adapters::IPushUpCounterAdapter {
public:
    void tick() override;
    const adapters::PushUpCounterState& state() const override;
    bool startSession(const std::string& sessionId, int totalReps, int dropThreshold, int upGap, int downTolerance) override;
    bool abortSession(const std::string& sessionId) override;
    bool consumeCompletion() override;

    void setConnected(bool connected);
    void setFirmwareReady(bool ready);
    void setTestMode(bool testMode);
    void simulateRepIncrement(int rep, int set = 1);
    void simulateCompletion();
    void simulateDisconnection();
    void setPortName(const std::string& port);

private:
    adapters::PushUpCounterState m_state;
    std::string m_currentSessionId;
    int m_totalReps{0};
    bool m_testMode{false};
};

}  // namespace companion::test
