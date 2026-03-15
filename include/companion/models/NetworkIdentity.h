#pragma once

#include <string>

namespace companion::models {

struct NetworkIdentity {
    std::string ipv4;
    std::string macAddress;
    std::string gatewayIpv4;
    std::string adapterName;
    std::string dnsIpv4;
    bool configuredThroughAirGateway{false};
};

}  // namespace companion::models
