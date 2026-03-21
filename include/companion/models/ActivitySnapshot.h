#pragma once

#include <string>
#include <vector>

#include "companion/models/NetworkIdentity.h"
#include "companion/models/RemoteAccessState.h"

namespace companion::models {

struct OpenAppEntry {
    std::string appName;
    std::string windowTitle;
};

struct ActivitySnapshot {
    std::string focusedApp;
    std::string focusedWindowTitle;
    std::string activeBrowserDomain;
    std::vector<OpenAppEntry> openApps;
    NetworkIdentity networkIdentity;
    RemoteAccessState remoteAccessState;
};

}  // namespace companion::models
