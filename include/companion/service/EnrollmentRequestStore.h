#pragma once

#include <optional>
#include <string>

namespace companion::service {

struct EnrollmentRequest {
    std::string baseUrl;
    std::string username;
    std::string password;
    std::string deviceLabel;
};

class EnrollmentRequestStore {
public:
    std::optional<EnrollmentRequest> load() const;
    bool saveTemplate() const;
    std::string requestPath() const;

private:
    static std::string requestDirectory();
};

}  // namespace companion::service
