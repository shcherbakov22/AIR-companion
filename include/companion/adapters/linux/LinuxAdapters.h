#pragma once

#include "companion/adapters/IScreenCaptureAdapter.h"

#include <optional>
#include <string>

namespace companion::adapters::linux {

class LinuxScreenCaptureAdapter final : public IScreenCaptureAdapter {
public:
    std::optional<std::string> captureToFile(const std::string& outputDirectory) override;
};

}  // namespace companion::adapters::linux
