#pragma once

#include <string>

namespace companion::models {

enum class DeviceCommandType {
    RefreshPolicy,
    RequestScreenshot,
    RequestCameraCapture,
    LockInternet,
    UnlockInternet,
    Unknown
};

struct DeviceCommand {
    std::string id;
    DeviceCommandType type{DeviceCommandType::Unknown};
    std::string status;
    std::string payloadJson;
};

}  // namespace companion::models
