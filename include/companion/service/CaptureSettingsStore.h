#pragma once

#include <string>

namespace companion::service {

struct InternalCaptureSettings {
    bool allowScreenCapture{true};
    bool allowCameraCapture{true};
    int minimumScreenIntervalSeconds{15};
    int minimumCameraIntervalSeconds{15};
    std::string screenOutputDirectory{"captures\\screen"};
    std::string cameraOutputDirectory{"captures\\camera"};
    std::string screenContentType{"image/jpeg"};
    std::string cameraContentType{"image/jpeg"};
};

class CaptureSettingsStore {
public:
    InternalCaptureSettings loadOrCreate() const;
    bool save(const InternalCaptureSettings& settings) const;
    std::string settingsPath() const;

private:
    static std::string settingsDirectory();
};

}  // namespace companion::service
