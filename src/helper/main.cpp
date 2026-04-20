#include "companion/adapters/windows/WindowsAdapters.h"

#include <iostream>

namespace {

const char* argumentValue(int argc, char* argv[], const char* expected) {
    for (int index = 1; index < argc - 1; ++index) {
        if (std::string(argv[index]) == expected) {
            return argv[index + 1];
        }
    }

    return nullptr;
}

int printUsage() {
    std::cerr
        << "AIR Companion helper modes:\n"
        << "  --capture-screen-once <output-directory>\n"
        << "  --snapshot-apps-once <output-file>\n";
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (const auto* outputDirectory = argumentValue(argc, argv, "--capture-screen-once"); outputDirectory != nullptr) {
        companion::adapters::windows::WindowsScreenCaptureAdapter screenCaptureAdapter;
        return screenCaptureAdapter.captureInteractive(outputDirectory).has_value() ? 0 : 1;
    }

    if (const auto* outputFile = argumentValue(argc, argv, "--snapshot-apps-once"); outputFile != nullptr) {
        companion::adapters::windows::WindowsAppTrackerAdapter appTrackerAdapter;
        return appTrackerAdapter.writeSnapshotToFile(outputFile) ? 0 : 1;
    }

    return printUsage();
}
