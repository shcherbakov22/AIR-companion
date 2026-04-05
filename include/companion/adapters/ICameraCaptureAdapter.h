#pragma once

#include <optional>
#include <string>

namespace companion::adapters {

class ICameraCaptureAdapter {
public:
    virtual ~ICameraCaptureAdapter() = default;

    virtual std::optional<std::string> captureToFile(const std::string& outputDirectory) = 0;
};

}  // namespace companion::adapters

