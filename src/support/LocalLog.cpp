#include "companion/support/LocalLog.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

namespace companion::support {

namespace {

std::mutex& logMutex() {
    static std::mutex mutex;
    return mutex;
}

const char* envValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count() % 1000;

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setw(3)
        << std::setfill('0')
        << millis;
    return out.str();
}

std::filesystem::path logDirectoryPath() {
    if (const char* overridePath = envValue("AIR_COMPANION_LOG_DIR")) {
        return std::filesystem::path(overridePath);
    }

#ifdef _WIN32
    if (const char* programData = envValue("ProgramData")) {
        return std::filesystem::path(programData) / "AIRCompanion" / "Logs";
    }
    if (const char* programData = envValue("PROGRAMDATA")) {
        return std::filesystem::path(programData) / "AIRCompanion" / "Logs";
    }
    if (const char* appData = envValue("APPDATA")) {
        return std::filesystem::path(appData) / "AIRCompanion" / "Logs";
    }
#endif

    return std::filesystem::current_path() / "AIRCompanion" / "Logs";
}

}  // namespace

void appendLocalLog(const std::string& fileName, const std::string& line) {
    if (fileName.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex());

    const auto directory = logDirectoryPath();
    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);
    if (errorCode) {
        return;
    }

    std::ofstream output(directory / fileName, std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << timestamp() << " " << line << '\n';
}

void appendDebugLog(const std::string& line) {
    appendLocalLog("debug.log", line);
}

std::string localLogDirectory() {
    return logDirectoryPath().string();
}

}  // namespace companion::support
