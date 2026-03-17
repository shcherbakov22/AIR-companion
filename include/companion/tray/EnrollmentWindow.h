#pragma once

#include <optional>
#include <string>

#include "companion/service/EnrollmentRequestStore.h"

namespace companion::tray {

class EnrollmentWindow {
public:
    static std::optional<service::EnrollmentRequest> prompt(
        const service::EnrollmentRequest& initial,
        const std::string& statusMessage = {}
    );
};

}  // namespace companion::tray
