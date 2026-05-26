#pragma once

#include <string>

namespace companion::support {

void appendLocalLog(const std::string& fileName, const std::string& line);
void appendDebugLog(const std::string& line);
std::string localLogDirectory();

}  // namespace companion::support
