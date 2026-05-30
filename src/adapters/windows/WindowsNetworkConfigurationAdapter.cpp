#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/support/LocalLog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace companion::adapters::windows {

namespace {

#ifdef _WIN32

std::string wideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(), required, nullptr, nullptr);
    output.resize(static_cast<std::size_t>(required - 1));
    return output;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), required);
    output.resize(static_cast<std::size_t>(required - 1));
    return output;
}

std::string sockaddrToIpv4String(const SOCKADDR* address) {
    if (address == nullptr || address->sa_family != AF_INET) {
        return {};
    }

    char buffer[INET_ADDRSTRLEN]{};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
    if (InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), buffer, sizeof(buffer)) == nullptr) {
        return {};
    }

    return std::string(buffer);
}

std::string macToString(const BYTE* address, ULONG length) {
    if (address == nullptr || length == 0) {
        return {};
    }

    std::ostringstream out;
    for (ULONG index = 0; index < length; ++index) {
        if (index > 0) {
            out << ":";
        }

        out.width(2);
        out.fill('0');
        out << std::hex << std::nouppercase << static_cast<int>(address[index]);
    }

    return out.str();
}

bool isUsableAdapter(const IP_ADAPTER_ADDRESSES& adapter) {
    if (adapter.OperStatus != IfOperStatusUp) {
        return false;
    }

    if (adapter.IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        return false;
    }

    return adapter.FirstUnicastAddress != nullptr;
}

std::string lastErrorString(const std::string& prefix, DWORD error) {
    std::ostringstream out;
    out << prefix << " error=" << error;
    return out.str();
}

bool setMobileHotspotPolicyDisabled() {
    HKEY key{};
    const auto result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows\\Network Connections",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("hotspot policy registry open failed", result));
        return false;
    }

    const DWORD disabled = 0;
    const auto setResult = RegSetValueExW(
        key,
        L"NC_ShowSharedAccessUI",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&disabled),
        sizeof(disabled)
    );
    RegCloseKey(key);

    if (setResult != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("hotspot policy registry write failed", setResult));
        return false;
    }

    return true;
}

bool setRegistryStringValue(const std::wstring& path, const std::wstring& name, const std::string& value) {
    HKEY key{};
    const auto result = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        path.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr,
        &key,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("browser extension policy registry open failed", result));
        return false;
    }

    const auto wideValue = utf8ToWide(value);
    const auto byteCount = static_cast<DWORD>((wideValue.size() + 1) * sizeof(wchar_t));
    const auto setResult = RegSetValueExW(
        key,
        name.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(wideValue.c_str()),
        byteCount
    );
    RegCloseKey(key);

    if (setResult != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("browser extension policy registry write failed", setResult));
        return false;
    }

    return true;
}

bool readRegistryStringValue(const std::wstring& path, const std::wstring& name, std::string& value) {
    HKEY key{};
    const auto openResult = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        path.c_str(),
        0,
        KEY_QUERY_VALUE | KEY_WOW64_64KEY,
        &key
    );

    if (openResult != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    auto queryResult = RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &byteCount);
    if (queryResult != ERROR_SUCCESS || type != REG_SZ || byteCount == 0) {
        RegCloseKey(key);
        return false;
    }

    std::wstring wideValue(byteCount / sizeof(wchar_t), L'\0');
    queryResult = RegQueryValueExW(
        key,
        name.c_str(),
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(wideValue.data()),
        &byteCount
    );
    RegCloseKey(key);

    if (queryResult != ERROR_SUCCESS || wideValue.empty()) {
        return false;
    }

    if (wideValue.back() == L'\0') {
        wideValue.pop_back();
    }

    value = wideToUtf8(wideValue.c_str());
    return true;
}

bool deleteRegistryValue(const std::wstring& path, const std::wstring& name) {
    HKEY key{};
    const auto openResult = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        path.c_str(),
        0,
        KEY_SET_VALUE | KEY_WOW64_64KEY,
        &key
    );

    if (openResult == ERROR_FILE_NOT_FOUND) {
        return true;
    }

    if (openResult != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("browser extension policy registry delete open failed", openResult));
        return false;
    }

    const auto deleteResult = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);

    if (deleteResult == ERROR_FILE_NOT_FOUND) {
        return true;
    }

    if (deleteResult != ERROR_SUCCESS) {
        companion::support::appendDebugLog(lastErrorString("browser extension policy registry delete failed", deleteResult));
        return false;
    }

    return true;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char character : value) {
        switch (character) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (character < 0x20) {
                    out << "\\u";
                    out.width(4);
                    out.fill('0');
                    out << std::hex << std::nouppercase << static_cast<int>(character);
                } else {
                    out << static_cast<char>(character);
                }
                break;
        }
    }

    return out.str();
}

std::string browserExtensionSettingsJson(const std::string& extensionId, const std::string& updateUrl) {
    std::ostringstream out;
    out << "{\"" << jsonEscape(extensionId) << "\":{"
        << "\"installation_mode\":\"force_installed\","
        << "\"toolbar_pin\":\"force_pinned\","
        << "\"update_url\":\"" << jsonEscape(updateUrl) << "\""
        << "}}";
    return out.str();
}

bool setBrowserExtensionManagedPolicy(
    const std::wstring& browserPolicyRoot,
    const std::string& extensionId,
    const std::string& platformUrl,
    const std::string& deviceToken) {
    const auto extensionIdWide = utf8ToWide(extensionId);
    if (extensionIdWide.empty()) {
        return false;
    }

    const auto policyRoot = browserPolicyRoot + L"\\3rdparty\\extensions\\" + extensionIdWide + L"\\policy";
    return setRegistryStringValue(policyRoot, L"platformUrl", platformUrl)
        && setRegistryStringValue(policyRoot, L"deviceToken", deviceToken);
}

bool clearLocalBrowserExtensionInstallPolicy(
    const std::wstring& browserPolicyRoot,
    const std::string& extensionId,
    const std::string& updateUrl) {
    const auto forceListRoot = browserPolicyRoot + L"\\ExtensionInstallForcelist";
    const auto expectedForceInstallValue = extensionId + ";" + updateUrl;
    bool ok = true;

    for (int index = 1; index <= 20; ++index) {
        std::string currentValue;
        const auto valueName = std::to_wstring(index);
        if (readRegistryStringValue(forceListRoot, valueName, currentValue)
            && currentValue == expectedForceInstallValue) {
            ok = deleteRegistryValue(forceListRoot, valueName) && ok;
        }
    }

    std::string extensionSettings;
    if (readRegistryStringValue(browserPolicyRoot, L"ExtensionSettings", extensionSettings)
        && extensionSettings.find(extensionId) != std::string::npos) {
        ok = deleteRegistryValue(browserPolicyRoot, L"ExtensionSettings") && ok;
    }

    return ok;
}

bool stopAndDisableService(const wchar_t* serviceName, const char* logName) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        companion::support::appendDebugLog(lastErrorString(std::string("hotspot service manager open failed service=") + logName, GetLastError()));
        return false;
    }

    SC_HANDLE service = OpenServiceW(
        manager,
        serviceName,
        SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_CHANGE_CONFIG
    );

    if (service == nullptr) {
        const auto error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            companion::support::appendDebugLog(std::string("hotspot service missing service=") + logName);
            return true;
        }

        companion::support::appendDebugLog(lastErrorString(std::string("hotspot service open failed service=") + logName, error));
        return false;
    }

    bool ok = true;
    if (!ChangeServiceConfigW(
            service,
            SERVICE_NO_CHANGE,
            SERVICE_DISABLED,
            SERVICE_NO_CHANGE,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        )) {
        companion::support::appendDebugLog(lastErrorString(std::string("hotspot service disable failed service=") + logName, GetLastError()));
        ok = false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded
        )) {
        if (status.dwCurrentState != SERVICE_STOPPED && status.dwCurrentState != SERVICE_STOP_PENDING) {
            SERVICE_STATUS stopStatus{};
            if (!ControlService(service, SERVICE_CONTROL_STOP, &stopStatus)) {
                const auto error = GetLastError();
                if (error != ERROR_SERVICE_NOT_ACTIVE) {
                    companion::support::appendDebugLog(lastErrorString(std::string("hotspot service stop failed service=") + logName, error));
                    ok = false;
                }
            }
        }
    } else {
        companion::support::appendDebugLog(lastErrorString(std::string("hotspot service status failed service=") + logName, GetLastError()));
        ok = false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
}

#endif

}  // namespace

models::NetworkIdentity WindowsNetworkConfigurationAdapter::currentIdentity() const {
    if (const auto detected = detectPrimaryIdentity(); detected.has_value()) {
        m_currentIdentity = *detected;

        if (m_originalIdentity.has_value()
            && m_currentIdentity.gatewayIpv4 == m_originalIdentity->gatewayIpv4
            && m_currentIdentity.dnsIpv4 == m_originalIdentity->dnsIpv4) {
            m_currentIdentity.configuredThroughAirGateway = false;
        }
    }

    return m_currentIdentity;
}

bool WindowsNetworkConfigurationAdapter::ensureAirGateway(const std::string& gatewayIpv4, const std::string& dnsIpv4) {
    (void) gatewayIpv4;
    (void) dnsIpv4;
    m_currentIdentity = currentIdentity();
    m_currentIdentity.configuredThroughAirGateway = false;
    m_state = "network passthrough";
    return true;
}

bool WindowsNetworkConfigurationAdapter::restorePreviousConfiguration() {
    m_currentIdentity = currentIdentity();
    m_currentIdentity.configuredThroughAirGateway = false;
    m_state = "network passthrough";
    return true;
}

bool WindowsNetworkConfigurationAdapter::enforceHotspotDisabled() {
#ifdef _WIN32
    const bool registryOk = setMobileHotspotPolicyDisabled();
    const bool hotspotServiceOk = stopAndDisableService(L"icssvc", "icssvc");
    const bool sharingServiceOk = stopAndDisableService(L"SharedAccess", "SharedAccess");
    const bool ok = registryOk && hotspotServiceOk && sharingServiceOk;

    m_state = ok ? "network passthrough; hotspot disabled" : "network passthrough; hotspot hardening failed";
    if (ok && !m_hotspotHardeningSuccessLogged) {
        companion::support::appendDebugLog("network hardening ok: mobile hotspot and ICS disabled");
        m_hotspotHardeningSuccessLogged = true;
    }

    return ok;
#else
    return true;
#endif
}

bool WindowsNetworkConfigurationAdapter::enforceBrowserExtensionEnterprisePolicy(
    const models::DevicePolicy::BrowserExtensionEnterprisePolicy& policy) {
#ifdef _WIN32
    if (!policy.enabled) {
        return true;
    }

    if (policy.extensionId.empty() || policy.updateUrl.empty() || policy.platformUrl.empty() || policy.deviceToken.empty()) {
        companion::support::appendDebugLog("browser extension policy repair skipped: incomplete policy");
        return false;
    }

    bool ok = true;
    const bool useCloudInstallPolicy = !policy.chromeEnterpriseEnrollmentToken.empty();

    if (useCloudInstallPolicy) {
        ok = setRegistryStringValue(
            L"SOFTWARE\\Policies\\Google\\Chrome",
            L"CloudManagementEnrollmentToken",
            policy.chromeEnterpriseEnrollmentToken
        ) && ok;

        ok = clearLocalBrowserExtensionInstallPolicy(
            L"SOFTWARE\\Policies\\Google\\Chrome",
            policy.extensionId,
            policy.updateUrl
        ) && ok;
        ok = clearLocalBrowserExtensionInstallPolicy(
            L"SOFTWARE\\Policies\\Microsoft\\Edge",
            policy.extensionId,
            policy.updateUrl
        ) && ok;
    } else {
        const auto forceInstallValue = policy.extensionId + ";" + policy.updateUrl;
        ok = setRegistryStringValue(
            L"SOFTWARE\\Policies\\Google\\Chrome\\ExtensionInstallForcelist",
            L"1",
            forceInstallValue
        ) && ok;
        ok = setRegistryStringValue(
            L"SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallForcelist",
            L"1",
            forceInstallValue
        ) && ok;

        const auto extensionSettings = browserExtensionSettingsJson(policy.extensionId, policy.updateUrl);
        ok = setRegistryStringValue(
            L"SOFTWARE\\Policies\\Google\\Chrome",
            L"ExtensionSettings",
            extensionSettings
        ) && ok;
        ok = setRegistryStringValue(
            L"SOFTWARE\\Policies\\Microsoft\\Edge",
            L"ExtensionSettings",
            extensionSettings
        ) && ok;
    }

    ok = setBrowserExtensionManagedPolicy(
        L"SOFTWARE\\Policies\\Google\\Chrome",
        policy.extensionId,
        policy.platformUrl,
        policy.deviceToken
    ) && ok;
    ok = setBrowserExtensionManagedPolicy(
        L"SOFTWARE\\Policies\\Microsoft\\Edge",
        policy.extensionId,
        policy.platformUrl,
        policy.deviceToken
    ) && ok;

    if (ok && !m_browserExtensionPolicySuccessLogged) {
        companion::support::appendDebugLog(
            useCloudInstallPolicy
                ? "browser extension policy repair ok: Chrome Enterprise Core token and managed config present; local force install cleared"
                : "browser extension policy repair ok: Chrome/Edge force install, ExtensionSettings, and managed config present"
        );
        m_browserExtensionPolicySuccessLogged = true;
    }

    return ok;
#else
    (void) policy;
    return true;
#endif
}

std::string WindowsNetworkConfigurationAdapter::describeState() const {
    return m_state;
}

bool WindowsNetworkConfigurationAdapter::captureOriginalConfiguration() {
    if (m_originalIdentity.has_value()) {
        return true;
    }

    const auto detected = detectPrimaryIdentity();
    if (!detected.has_value()) {
        return false;
    }

    m_originalIdentity = *detected;
    return true;
}

std::optional<models::NetworkIdentity> WindowsNetworkConfigurationAdapter::detectPrimaryIdentity() const {
#ifdef _WIN32
    ULONG bufferSize = 16 * 1024;
    std::vector<unsigned char> buffer(bufferSize);

    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &bufferSize);
    }

    if (result != NO_ERROR) {
        return std::nullopt;
    }

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (!isUsableAdapter(*adapter)) {
            continue;
        }

        models::NetworkIdentity identity;
        identity.adapterName = wideToUtf8(adapter->FriendlyName);
        identity.macAddress = macToString(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
        identity.interfaceIndex = adapter->IfIndex;

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            const auto ipv4 = sockaddrToIpv4String(unicast->Address.lpSockaddr);
            if (!ipv4.empty()) {
                identity.ipv4 = ipv4;
                break;
            }
        }

        for (auto* gateway = adapter->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next) {
            const auto gatewayIpv4 = sockaddrToIpv4String(gateway->Address.lpSockaddr);
            if (!gatewayIpv4.empty()) {
                identity.gatewayIpv4 = gatewayIpv4;
                break;
            }
        }

        for (auto* dns = adapter->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
            const auto dnsIpv4 = sockaddrToIpv4String(dns->Address.lpSockaddr);
            if (!dnsIpv4.empty()) {
                identity.dnsIpv4 = dnsIpv4;
                break;
            }
        }

        if (!identity.ipv4.empty()) {
            return identity;
        }
    }
#endif

    return std::nullopt;
}

bool WindowsNetworkConfigurationAdapter::applyDefaultRoute(const std::string& gatewayIpv4) const {
    if (m_currentIdentity.interfaceIndex == 0 || gatewayIpv4.empty()) {
        return false;
    }

    const auto deleteCommand =
        "cmd /c route delete 0.0.0.0 mask 0.0.0.0 if " + std::to_string(m_currentIdentity.interfaceIndex);
    (void)runCommand(deleteCommand);

    const auto addCommand =
        "cmd /c route add 0.0.0.0 mask 0.0.0.0 " + gatewayIpv4
        + " metric 1 if " + std::to_string(m_currentIdentity.interfaceIndex);

    return runCommand(addCommand);
}

bool WindowsNetworkConfigurationAdapter::applyDnsServer(const std::string& dnsIpv4) const {
    if (m_currentIdentity.adapterName.empty() || dnsIpv4.empty()) {
        return false;
    }

    const auto command =
        "cmd /c netsh interface ipv4 set dnsservers name="
        + quoteForCommand(m_currentIdentity.adapterName)
        + " static " + dnsIpv4 + " primary validate=no";

    return runCommand(command);
}

std::string WindowsNetworkConfigurationAdapter::quoteForCommand(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped += "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

bool WindowsNetworkConfigurationAdapter::runCommand(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

}  // namespace companion::adapters::windows
