#pragma once

#include <string>

namespace companion::tray {

int runRemoteControlHelper(int port, const std::string& stateFilePath);

}  // namespace companion::tray
