#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

models::NetworkIdentity WindowsNetworkConfigurationAdapter::currentIdentity() const {
    return m_currentIdentity;
}

bool WindowsNetworkConfigurationAdapter::ensureAirGateway(const std::string& gatewayIpv4, const std::string& dnsIpv4) {
    m_currentIdentity.gatewayIpv4 = gatewayIpv4;
    m_currentIdentity.dnsIpv4 = dnsIpv4;
    m_currentIdentity.configuredThroughAirGateway = true;
    m_state = "gateway " + gatewayIpv4 + " dns " + dnsIpv4;
    return true;
}

bool WindowsNetworkConfigurationAdapter::restorePreviousConfiguration() {
    m_currentIdentity.gatewayIpv4 = "192.168.11.1";
    m_currentIdentity.dnsIpv4 = "192.168.11.1";
    m_currentIdentity.configuredThroughAirGateway = false;
    m_state = "network passthrough";
    return true;
}

std::string WindowsNetworkConfigurationAdapter::describeState() const {
    return m_state;
}

}  // namespace companion::adapters::windows
