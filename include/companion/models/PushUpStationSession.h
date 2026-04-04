#pragma once

#include <string>

namespace companion::models {

struct PushUpStationSession {
    std::string id;
    std::string status;
    std::string studentName;
    int requiredPushUps{0};
    int currentRep{0};
    int currentSet{1};
    int dropThreshold{20};
    int upGap{6};
    int downTolerance{3};
};

}  // namespace companion::models
