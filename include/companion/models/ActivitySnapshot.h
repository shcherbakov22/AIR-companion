#pragma once

#include <string>
#include <vector>

#include "companion/models/NetworkIdentity.h"
#include "companion/models/RemoteAccessState.h"

namespace companion::models {

struct ActivitySnapshot {
    std::string focusedApp;
    std::string focusedWindowTitle;
    std::string activeBrowserDomain;
    std::vector<std::string> openApps;
    NetworkIdentity networkIdentity;
    RemoteAccessState remoteAccessState;
};

}  // namespace companion::models
