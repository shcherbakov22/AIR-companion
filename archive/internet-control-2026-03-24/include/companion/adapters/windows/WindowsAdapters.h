#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "companion/adapters/IBrowserDomainAdapter.h"
#include "companion/adapters/ICameraCaptureAdapter.h"
#include "companion/adapters/IEnforcementAdapter.h"
#include "companion/adapters/IAppTrackerAdapter.h"
#include "companion/adapters/INetworkConfigurationAdapter.h"
#include "companion/adapters/IRemoteAccessAdapter.h"
#include "companion/adapters/IScreenCaptureAdapter.h"
#include "companion/adapters/IServiceLifecycleAdapter.h"

namespace companion::adapters::windows {

class WindowsScreenCaptureAdapter final : public IScreenCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;

private:
    std::optional<std::string> captureInteractive(const std::string& outputDirectory) const;
    std::optional<std::string> captureViaActiveSessionHelper(const std::string& outputDirectory) const;
};

class WindowsCameraCaptureAdapter final : public ICameraCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;
};

class WindowsAppTrackerAdapter final : public IAppTrackerAdapter {
public:
    models::ActivitySnapshot snapshot() const override;
    std::vector<models::InstalledAppEntry> installedApps() const override;
    bool writeSnapshotToFile(const std::string& outputPath) const;

private:
    models::ActivitySnapshot collectInteractiveSnapshot() const;
    std::optional<models::ActivitySnapshot> captureViaActiveSessionHelper() const;
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

class WindowsRemoteAccessAdapter final : public IRemoteAccessAdapter {
public:
    models::RemoteAccessState currentState() const override;
    bool startRemoteControl() override;
    bool stopRemoteControl() override;
    bool verifyReadiness() override;

private:
    bool helperIsListening() const;
    bool activeConsoleSessionAvailable() const;
    bool launchHelper();
    std::wstring helperBinaryPath() const;
    std::wstring helperStatePath() const;
    std::optional<std::uint32_t> helperProcessId() const;
    static bool processExists(std::uint32_t processId);
    bool isTcpPortListening(unsigned short port) const;
    bool waitForHelperReady(int timeoutMilliseconds);
    static bool runCommand(const std::string& command);
    static std::string quoteForCommand(const std::string& value);

    models::RemoteAccessState m_state{};
};

class WindowsNetworkConfigurationAdapter final : public INetworkConfigurationAdapter {
public:
    models::NetworkIdentity currentIdentity() const override;
    bool ensureAirGateway(const std::string& gatewayIpv4, const std::string& dnsIpv4) override;
    bool restorePreviousConfiguration() override;
    std::string describeState() const override;

private:
    bool captureOriginalConfiguration();
    std::optional<models::NetworkIdentity> detectPrimaryIdentity() const;
    bool applyDefaultRoute(const std::string& gatewayIpv4) const;
    bool applyDnsServer(const std::string& dnsIpv4) const;
    static std::string quoteForCommand(const std::string& value);
    static bool runCommand(const std::string& command);

    mutable models::NetworkIdentity m_currentIdentity{};
    std::optional<models::NetworkIdentity> m_originalIdentity;
    std::string m_state{"network passthrough"};
};

}  // namespace companion::adapters::windows
