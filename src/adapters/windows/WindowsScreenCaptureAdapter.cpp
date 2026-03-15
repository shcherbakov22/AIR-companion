#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

std::optional<std::string> WindowsScreenCaptureAdapter::captureToFile(const std::string& outputDirectory) {
    return outputDirectory + "/screen-stub.png";
}

}  // namespace companion::adapters::windows

