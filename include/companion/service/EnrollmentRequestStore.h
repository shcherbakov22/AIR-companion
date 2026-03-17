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
    std::optional<EnrollmentRequest> loadDraft() const;
    std::optional<EnrollmentRequest> load() const;
    bool save(const EnrollmentRequest& request) const;
    bool saveTemplate() const;
    bool clear() const;
    std::string requestPath() const;

private:
    static std::string requestDirectory();
};

}  // namespace companion::service
