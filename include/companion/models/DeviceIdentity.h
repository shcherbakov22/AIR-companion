#pragma once

#include <string>

namespace companion::models {

struct DeviceIdentity {
    std::string deviceId;
    std::string hostname;
    std::string deviceLabel;
    std::string platform;
    std::string appVersion;
    std::string studentUsername;
};

struct DeviceEnrollment {
    DeviceIdentity identity;
    std::string deviceToken;
};

}  // namespace companion::models

