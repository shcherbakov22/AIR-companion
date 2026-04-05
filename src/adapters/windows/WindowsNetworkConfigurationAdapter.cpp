#include "companion/adapters/windows/WindowsAdapters.h"

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

    std::string output(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(), required, nullptr, nullptr);
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
