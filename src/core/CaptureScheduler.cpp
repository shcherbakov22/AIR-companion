#include "companion/core/CaptureScheduler.h"

namespace companion::core {

namespace {

bool ready(std::chrono::steady_clock::time_point last,
           std::chrono::steady_clock::time_point now,
           int intervalSeconds) {
    if (last == std::chrono::steady_clock::time_point{}) {
        return true;
    }

    return now - last >= std::chrono::seconds(intervalSeconds);
}

}  // namespace

void CaptureScheduler::updatePolicy(const models::DevicePolicy& policy) {
    m_captureScreen = policy.shouldCaptureScreen;
    m_captureCamera = policy.shouldCaptureCamera;
    m_screenIntervalSeconds = policy.screenCaptureIntervalSeconds;
    m_cameraIntervalSeconds = policy.cameraCaptureIntervalSeconds;
}

bool CaptureScheduler::shouldCaptureScreen(std::chrono::steady_clock::time_point now) const {
    return m_captureScreen && ready(m_lastScreenCapture, now, m_screenIntervalSeconds);
}

bool CaptureScheduler::shouldCaptureCamera(std::chrono::steady_clock::time_point now) const {
    return m_captureCamera && ready(m_lastCameraCapture, now, m_cameraIntervalSeconds);
}

void CaptureScheduler::markScreenCaptured(std::chrono::steady_clock::time_point now) {
    m_lastScreenCapture = now;
}

void CaptureScheduler::markCameraCaptured(std::chrono::steady_clock::time_point now) {
    m_lastCameraCapture = now;
}

}  // namespace companion::core

