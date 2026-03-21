#pragma once

#include <string>

namespace companion::models {

struct RemoteAccessState {
    bool ready{false};
    bool active{false};
    int port{0};
    std::string failureReason;
};

}  // namespace companion::models
