#include "companion/service/BootAutoStartRegistrar.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace companion::service {

namespace {

#ifdef _WIN32

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
    return result;
}

std::string currentExecutablePath() {
    std::wstring path(4096, L'\0');
    const auto copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (copied == 0) {
        return {};
    }

    path.resize(copied);
    return wideToUtf8(path);
}

bool runProcess(const std::wstring& commandLine) {
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommandLine = commandLine;

    const auto created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (!created) {
        return false;
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return exitCode == 0;
}

#endif

}  // namespace

bool BootAutoStartRegistrar::ensureEnabled() const {
#ifdef _WIN32
    const auto currentPath = currentExecutablePath();
    if (currentPath.empty()) {
        return false;
    }

    const auto servicePath = serviceBinaryPathForExecutable(currentPath);
    if (servicePath.empty() || !std::filesystem::exists(std::filesystem::path(servicePath))) {
        return false;
    }

    const auto wideTaskName = utf8ToWide(taskName());
    const auto wideServicePath = utf8ToWide(servicePath);
    const std::wstring commandLine =
        L"schtasks.exe /Create /TN \"" + wideTaskName
        + L"\" /SC ONSTART /RU SYSTEM /RL HIGHEST /TR \"\\\"" + wideServicePath + L"\\\"\" /F";

    return runProcess(commandLine);
#else
    return false;
#endif
}

std::string BootAutoStartRegistrar::taskName() {
    return "AIR Companion";
}

std::string BootAutoStartRegistrar::serviceBinaryPathForExecutable(const std::string& executablePath) {
    if (executablePath.empty()) {
        return {};
    }

    auto path = std::filesystem::path(executablePath);
    auto filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (filename == "air_companion_service.exe") {
        return path.string();
    }

    path.replace_filename("air_companion_service.exe");
    return path.string();
}

}  // namespace companion::service
