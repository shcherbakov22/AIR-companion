#pragma once

#include <optional>
#include <string>

#include "companion/adapters/IBrowserDomainAdapter.h"
#include "companion/adapters/ICameraCaptureAdapter.h"
#include "companion/adapters/IEnforcementAdapter.h"
#include "companion/adapters/IAppTrackerAdapter.h"
#include "companion/adapters/IScreenCaptureAdapter.h"
#include "companion/adapters/IServiceLifecycleAdapter.h"

namespace companion::adapters::windows {

class WindowsScreenCaptureAdapter final : public IScreenCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;
};

class WindowsCameraCaptureAdapter final : public ICameraCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;
};

class WindowsAppTrackerAdapter final : public IAppTrackerAdapter {
public:
    models::ActivitySnapshot snapshot() const override;
};

class WindowsBrowserDomainAdapter final : public IBrowserDomainAdapter {
public:
    std::optional<std::string> activeDomain() const override;
};

class WindowsEnforcementAdapter final : public IEnforcementAdapter {
public:
    void applyPolicy(const models::DevicePolicy& policy) override;
    void terminateBlockedApps(const std::vector<std::string>& blockedApps) override;
    std::string describeState() const override;

private:
    std::string m_lastState{"idle"};
};

class WindowsServiceLifecycleAdapter final : public IServiceLifecycleAdapter {
public:
    bool install() override;
    bool start() override;
    bool stop() override;
};

}  // namespace companion::adapters::windows
