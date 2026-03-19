#pragma once

#include <string>

namespace companion::service {

class BootAutoStartRegistrar {
public:
    bool ensureEnabled() const;

    static std::string taskName();
    static std::string serviceBinaryPathForExecutable(const std::string& executablePath);
    static std::string taskXmlForServiceBinary(const std::string& serviceBinaryPath);
};

}  // namespace companion::service
