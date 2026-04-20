#include "companion/adapters/linux/LinuxAdapters.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace companion::adapters::linux {

namespace {

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

bool commandExists(const std::string& command) {
    const auto probe = "command -v " + shellQuote(command) + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

void appendDebugLog(const std::string& line) {
    const auto stateHome = [] {
        if (const auto* value = std::getenv("XDG_STATE_HOME"); value != nullptr && *value != '\0') {
            return std::filesystem::path(value);
        }
        if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            return std::filesystem::path(home) / ".local" / "state";
        }
        return std::filesystem::path(".");
    }();

    std::error_code errorCode;
    const auto logDirectory = stateHome / "AIRCompanion";
    std::filesystem::create_directories(logDirectory, errorCode);
    std::ofstream output(logDirectory / "linux-screenshot.log", std::ios::app);
    if (output.is_open()) {
        output << line << '\n';
    }
}

std::string timestampSuffix() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return std::to_string(millis);
}

bool hasUsableFile(const std::filesystem::path& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode)
        && std::filesystem::is_regular_file(path, errorCode)
        && std::filesystem::file_size(path, errorCode) > 0;
}

int runCaptureCommand(const std::string& command) {
    const auto boundedCommand = commandExists("timeout")
        ? "timeout 20s " + command
        : command;
    return std::system((boundedCommand + " >/dev/null 2>&1").c_str());
}

}  // namespace

std::optional<std::string> LinuxScreenCaptureAdapter::captureToFile(const std::string& outputDirectory) {
    std::error_code errorCode;
    const auto directory = std::filesystem::path(outputDirectory);
    std::filesystem::create_directories(directory, errorCode);
    if (errorCode) {
        appendDebugLog("failed to create capture directory: " + outputDirectory);
        return std::nullopt;
    }

    const auto outputPath = directory / ("screen-" + timestampSuffix() + ".png");
    const auto quotedOutput = shellQuote(outputPath.string());

    struct Candidate {
        std::string binary;
        std::string command;
    };

    const std::vector<Candidate> candidates{
        {"gnome-screenshot", "gnome-screenshot -f " + quotedOutput},
        {"grim", "grim " + quotedOutput},
        {"spectacle", "spectacle -b -n -o " + quotedOutput},
        {"scrot", "scrot " + quotedOutput},
        {"maim", "maim " + quotedOutput},
        {"import", "import -window root " + quotedOutput},
    };

    for (const auto& candidate : candidates) {
        if (!commandExists(candidate.binary)) {
            continue;
        }

        appendDebugLog("capture trying " + candidate.binary);
        const auto result = runCaptureCommand(candidate.command);
        if (result == 0 && hasUsableFile(outputPath)) {
            appendDebugLog("capture succeeded path=" + outputPath.string());
            return outputPath.string();
        }

        std::filesystem::remove(outputPath, errorCode);
        appendDebugLog("capture failed binary=" + candidate.binary + " result=" + std::to_string(result));
    }

    appendDebugLog("capture failed: no supported screenshot backend succeeded");
    return std::nullopt;
}

}  // namespace companion::adapters::linux
