#include "companion/adapters/linux/LinuxAdapters.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <array>
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

std::optional<std::string> commandOutput(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;

    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    if (pclose(pipe) != 0) {
        return std::nullopt;
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ' || output.back() == '\t')) {
        output.pop_back();
    }

    return output.empty() ? std::nullopt : std::optional<std::string>(output);
}

bool isKdeSession() {
    const auto* currentDesktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (currentDesktop != nullptr) {
        const std::string value(currentDesktop);
        if (value.find("KDE") != std::string::npos || value.find("PLASMA") != std::string::npos) {
            return true;
        }
    }

    const auto* kdeFullSession = std::getenv("KDE_FULL_SESSION");
    return kdeFullSession != nullptr && std::string(kdeFullSession) == "true";
}

std::optional<std::string> kdeDesktopCommand() {
    if (!isKdeSession()) {
        return std::nullopt;
    }

    const std::vector<std::string> candidates{
        "qdbus6 org.kde.KWin /KWin",
        "qdbus org.kde.KWin /KWin",
        "dbus-send --session --print-reply=literal --dest=org.kde.KWin /KWin",
    };

    for (const auto& candidate : candidates) {
        const auto executable = candidate.substr(0, candidate.find(' '));
        if (commandExists(executable)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::optional<int> currentKdeDesktop() {
    const auto dbusCommand = kdeDesktopCommand();
    if (!dbusCommand.has_value()) {
        return std::nullopt;
    }

    const auto output = commandOutput(*dbusCommand + " org.kde.KWin.currentDesktop");
    if (!output.has_value()) {
        return std::nullopt;
    }

    try {
        return std::stoi(*output);
    } catch (...) {
        return std::nullopt;
    }
}

std::string wrapForFirstKdeDesktop(const std::string& command) {
    const auto dbusCommand = kdeDesktopCommand();
    const auto currentDesktop = currentKdeDesktop();
    if (!dbusCommand.has_value() || !currentDesktop.has_value() || *currentDesktop <= 0) {
        return command;
    }

    if (*currentDesktop == 1) {
        return command;
    }

    appendDebugLog("capture switching KDE desktop from " + std::to_string(*currentDesktop) + " to 1");

    std::ostringstream script;
    script
        << "current_desktop=" << *currentDesktop << ";"
        << "cleanup(){ " << *dbusCommand << " org.kde.KWin.setCurrentDesktop \"$current_desktop\" >/dev/null 2>&1 || true; };"
        << "trap cleanup EXIT;"
        << *dbusCommand << " org.kde.KWin.setCurrentDesktop 1 >/dev/null 2>&1 || exit 1;"
        << "sleep 0.35;"
        << command << ";"
        << "status=$?;"
        << "exit $status";

    return "bash -lc " + shellQuote(script.str());
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
        const auto result = runCaptureCommand(wrapForFirstKdeDesktop(candidate.command));
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
