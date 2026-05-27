#pragma once

#include <string>

#include "companion/models/NetworkIdentity.h"

namespace companion::adapters {

class INetworkConfigurationAdapter {
public:
    virtual ~INetworkConfigurationAdapter() = default;

    virtual models::NetworkIdentity currentIdentity() const = 0;
    virtual bool ensureAirGateway(const std::string& gatewayIpv4, const std::string& dnsIpv4) = 0;
    virtual bool restorePreviousConfiguration() = 0;
    virtual bool enforceHotspotDisabled() { return true; }
    virtual std::string describeState() const = 0;
};

}  // namespace companion::adapters
