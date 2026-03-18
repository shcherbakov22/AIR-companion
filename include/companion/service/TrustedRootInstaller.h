#pragma once

#include <string>

namespace companion::service {

class TrustedRootInstaller {
public:
    bool ensureTrustedForBaseUrl(const std::string& baseUrl, const std::string& certificateUrl = {}) const;

    static std::string rootCertificateUrlForBaseUrl(const std::string& baseUrl);
};

}  // namespace companion::service
