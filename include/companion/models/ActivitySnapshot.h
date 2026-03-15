#pragma once

#include <string>
#include <vector>

namespace companion::models {

struct ActivitySnapshot {
    std::string focusedApp;
    std::string focusedWindowTitle;
    std::string activeBrowserDomain;
    std::vector<std::string> openApps;
};

}  // namespace companion::models

