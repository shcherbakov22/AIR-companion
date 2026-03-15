#pragma once

#include <chrono>

#include "companion/models/DevicePolicy.h"
#include "companion/service/CaptureSettingsStore.h"

namespace companion::core {

class CaptureScheduler {
public:
    explicit CaptureScheduler(service::InternalCaptureSettings settings = {});

    void updatePolicy(const models::DevicePolicy& policy);
    bool shouldCaptureScreen(std::chrono::steady_clock::time_point now) const;
    bool shouldCaptureCamera(std::chrono::steady_clock::time_point now) const;
    void markScreenCaptured(std::chrono::steady_clock::time_point now);
    void markCameraCaptured(std::chrono::steady_clock::time_point now);
    const service::InternalCaptureSettings& settings() const;

private:
    service::InternalCaptureSettings m_settings;
    int m_screenIntervalSeconds{30};
    int m_cameraIntervalSeconds{60};
    bool m_captureScreen{true};
    bool m_captureCamera{false};
    std::chrono::steady_clock::time_point m_lastScreenCapture{};
    std::chrono::steady_clock::time_point m_lastCameraCapture{};
};

}  // namespace companion::core
