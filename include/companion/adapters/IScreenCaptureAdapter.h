#pragma once

#include <optional>
#include <string>

namespace companion::adapters {

class IScreenCaptureAdapter {
public:
    virtual ~IScreenCaptureAdapter() = default;

    virtual std::optional<std::string> captureToFile(const std::string& outputDirectory) = 0;
};

}  // namespace companion::adapters

