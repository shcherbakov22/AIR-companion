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

struct InstalledAppEntry {
    std::string appName;
    std::string displayName;
    std::string displayVersion;
    std::string publisher;
    std::string installLocation;
    std::string source;
};

struct ActivitySnapshot {
    std::string focusedApp;
    std::string focusedWindowTitle;
    std::string activeBrowserDomain;
    std::vector<OpenAppEntry> openApps;
    std::vector<InstalledAppEntry> installedApps;
    NetworkIdentity networkIdentity;
    RemoteAccessState remoteAccessState;
};

}  // namespace companion::models
