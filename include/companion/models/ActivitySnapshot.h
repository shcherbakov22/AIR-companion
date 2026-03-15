#pragma once

#include <string>
#include <vector>

#include "companion/models/NetworkIdentity.h"

namespace companion::models {

struct ActivitySnapshot {
    std::string focusedApp;
    std::string focusedWindowTitle;
    std::string activeBrowserDomain;
    std::vector<std::string> openApps;
    NetworkIdentity networkIdentity;
};

}  // namespace companion::models
