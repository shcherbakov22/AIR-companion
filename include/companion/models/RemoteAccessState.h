#pragma once

#include <string>

namespace companion::models {

struct RemoteAccessState {
    bool ready{false};
    std::string failureReason;
    std::string username;
};

}  // namespace companion::models
