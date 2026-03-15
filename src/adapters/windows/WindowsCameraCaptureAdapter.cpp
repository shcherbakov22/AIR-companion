#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

std::optional<std::string> WindowsCameraCaptureAdapter::captureToFile(const std::string& outputDirectory) {
    return outputDirectory + "/camera-stub.png";
}

}  // namespace companion::adapters::windows

