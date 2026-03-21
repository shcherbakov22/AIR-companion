#pragma once

#include <map>
#include <string>

namespace companion::networking {

struct HttpRequestOptions {
    bool allowInvalidCertificate{false};
};

struct HttpResponse {
    int statusCode{0};
    std::string body;
};

class HttpClient {
public:
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     const HttpRequestOptions& options = {}) const;
    HttpResponse post(const std::string& url,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body,
                      const HttpRequestOptions& options = {}) const;
    HttpResponse postMultipart(const std::string& url,
                               const std::map<std::string, std::string>& headers,
                               const std::map<std::string, std::string>& fields,
                               const std::string& fileFieldName,
                               const std::string& filePath,
                               const std::string& contentType,
                               const HttpRequestOptions& options = {}) const;
    bool downloadToFile(const std::string& url,
                        const std::map<std::string, std::string>& headers,
                        const std::string& filePath,
                        const HttpRequestOptions& options = {}) const;
};

}  // namespace companion::networking
