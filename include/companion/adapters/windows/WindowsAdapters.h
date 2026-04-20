#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "companion/adapters/IBrowserDomainAdapter.h"
#include "companion/adapters/ICameraCaptureAdapter.h"
#include "companion/adapters/IEnforcementAdapter.h"
#include "companion/adapters/IAppTrackerAdapter.h"
#include "companion/adapters/INetworkConfigurationAdapter.h"
#include "companion/adapters/IPushUpCounterAdapter.h"
#include "companion/adapters/IRemoteAccessAdapter.h"
#include "companion/adapters/IScreenCaptureAdapter.h"
#include "companion/adapters/IServiceLifecycleAdapter.h"

namespace companion::adapters::windows {

class WindowsScreenCaptureAdapter final : public IScreenCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;
    std::optional<std::string> captureInteractive(const std::string& outputDirectory) const;

private:
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
    void applyPolicy(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot) override;
    void terminateBlockedApps(const std::vector<std::string>& blockedApps) override;
    bool showMessage(const std::string& title, const std::string& body, int displaySeconds, std::string& error) override;
    std::string describeState() const override;

private:
    std::unordered_set<std::string> violationKillTargets(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot);

    std::string m_lastState{"idle"};
    std::unordered_set<std::string> m_lastObservedOpenApps;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_browserGraceUntil;
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
    bool ensureFirewallRule() const;
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

class WindowsPushUpCounterAdapter final : public IPushUpCounterAdapter {
public:
    void tick() override;
    const PushUpCounterState& state() const override;
    bool startSession(const std::string& sessionId, int totalReps, int dropThreshold, int upGap, int downTolerance) override;
    bool abortSession(const std::string& sessionId) override;
    void hardReset() override;
    bool consumeCompletion() override;
    void processLine(const std::string& line) override;
    void resetAfterInactivity() override;

private:
    bool connectIfNeeded();
    void disconnect();
    void processIncoming();
    void pumpIncomingFor(std::chrono::milliseconds duration);
    void parseLine(const std::string& line);
    bool sendLine(const std::string& line);
    void checkInactivityReset();
    void hardResetArduino();

    static constexpr auto kInactivityTimeout = std::chrono::seconds(60);

    void* m_handle{nullptr};
    PushUpCounterState m_state{};
    std::string m_buffer;
    std::chrono::steady_clock::time_point m_lastScanAt{};
    std::chrono::steady_clock::time_point m_lastCommunicationAt{};
};

}  // namespace companion::adapters::windows
