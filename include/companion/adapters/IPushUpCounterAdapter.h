#pragma once

#include <optional>
#include <string>

namespace companion::adapters {

struct PushUpCounterState {
    bool connected{false};
    bool firmwareReady{false};
    std::string portName;
    std::string status{"disconnected"};
    int distance{-1};
    int currentRep{0};
    int currentSet{1};
    bool searchingBack{false};
    bool working{false};
    bool completionPending{false};
};

class IPushUpCounterAdapter {
public:
    virtual ~IPushUpCounterAdapter() = default;

    virtual void tick() = 0;
    virtual const PushUpCounterState& state() const = 0;
    virtual bool startSession(const std::string& sessionId, int totalReps, int dropThreshold, int upGap, int downTolerance) = 0;
    virtual bool abortSession(const std::string& sessionId) = 0;
    virtual bool consumeCompletion() = 0;
    virtual void processLine(const std::string& line) = 0;
    virtual void resetAfterInactivity() = 0;
};

}  // namespace companion::adapters
