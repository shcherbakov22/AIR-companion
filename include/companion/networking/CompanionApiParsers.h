#pragma once

#include <optional>
#include <string>
#include <vector>

#include "companion/models/DeviceCommand.h"
#include "companion/models/DeviceIdentity.h"
#include "companion/models/DevicePolicy.h"

namespace companion::networking {

std::optional<models::DeviceEnrollment> parseEnrollmentResponse(
    const std::string& responseBody,
    const models::DeviceIdentity& identity,
    const std::string& username
);

std::optional<std::string> parseRenewTokenResponse(const std::string& responseBody);
std::optional<models::DevicePolicy> parsePolicyResponse(const std::string& responseBody);
std::vector<models::DeviceCommand> parseCommandResponse(const std::string& responseBody);

}  // namespace companion::networking
