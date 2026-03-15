#include "companion/adapters/windows/WindowsAdapters.h"

#include <filesystem>
#include <fstream>

namespace companion::adapters::windows {

std::optional<std::string> WindowsScreenCaptureAdapter::captureToFile(const std::string& outputDirectory) {
    std::filesystem::create_directories(outputDirectory);
    const auto path = outputDirectory + "/screen-stub.png";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "AIR companion screen stub";
    if (!output.good()) {
        return std::nullopt;
    }
    return path;
}

}  // namespace companion::adapters::windows
