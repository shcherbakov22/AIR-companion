#pragma once

#include <string>
#include <vector>

namespace companion::models {

enum class InternetAccessMode {
    AllowAll,
    BlockAll,
    AllowListOnly
};

struct DevicePolicy {
    struct ViolationAppEnforcement {
        bool killGuiApps{false};
        int browserReopenGraceSeconds{60};
    };

    struct BrowserExtensionEnterprisePolicy {
        bool enabled{false};
        std::string extensionId;
        std::string updateUrl;
        std::string platformUrl;
        std::string deviceToken;
        std::string chromeEnterpriseEnrollmentToken;
    };

    std::string policyHash;
    std::string studentDisplayName;
    std::string activeScheduleName;
    std::string activeTaskName;
    bool hasUnreadMentorChat{false};
    bool hasUnreadAnnouncements{false};
    bool hasOpenViolations{false};
    bool browserTrackingEnabled{true};
    bool shouldCaptureScreen{true};
    bool shouldCaptureCamera{false};
    int screenCaptureIntervalSeconds{30};
    int cameraCaptureIntervalSeconds{60};
    InternetAccessMode internetAccessMode{InternetAccessMode::BlockAll};
    std::vector<std::string> allowedDomains;
    std::vector<std::string> blockedApps;
    ViolationAppEnforcement violationAppEnforcement{};
    BrowserExtensionEnterprisePolicy browserExtensionEnterprisePolicy{};
};

}  // namespace companion::models

