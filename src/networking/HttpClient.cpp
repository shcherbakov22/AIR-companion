#include "companion/networking/HttpClient.h"

namespace companion::networking {

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>&) const {
    return HttpResponse{200, "{\"stub\":true,\"url\":\"" + url + "\"}"};
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::map<std::string, std::string>&,
                              const std::string& body) const {
    return HttpResponse{200, "{\"stub\":true,\"url\":\"" + url + "\",\"body\":\"" + body + "\"}"};
}

}  // namespace companion::networking

