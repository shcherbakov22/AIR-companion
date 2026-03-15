#pragma once

#include <map>
#include <string>

namespace companion::networking {

struct HttpResponse {
    int statusCode{0};
    std::string body;
};

class HttpClient {
public:
    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers) const;
    HttpResponse post(const std::string& url,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body) const;
};

}  // namespace companion::networking

