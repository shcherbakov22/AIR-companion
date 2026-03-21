#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

struct Options {
    std::string serviceName;
    std::string packagePath;
    std::string targetDirectory;
};

std::optional<Options> parseArgs(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--service-name" && index + 1 < argc) {
            options.serviceName = argv[++index];
        } else if (arg == "--package" && index + 1 < argc) {
            options.packagePath = argv[++index];
        } else if (arg == "--target-dir" && index + 1 < argc) {
            options.targetDirectory = argv[++index];
        }
    }

    if (options.serviceName.empty() || options.packagePath.empty() || options.targetDirectory.empty()) {
        return std::nullopt;
    }

    return options;
}

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

bool waitForState(SC_HANDLE service, DWORD desiredState, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        if (!QueryServiceStatusEx(
                service,
                SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&status),
                sizeof(status),
                &bytesNeeded)) {
            return false;
        }

        if (status.dwCurrentState == desiredState) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return false;
}

bool stopService(const std::wstring& serviceName) {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return false;
    }

    const auto service = OpenServiceW(manager, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded)) {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
    }

    if (status.dwCurrentState != SERVICE_STOPPED) {
        SERVICE_STATUS ignored{};
        ControlService(service, SERVICE_CONTROL_STOP, &ignored);
        if (!waitForState(service, SERVICE_STOPPED, std::chrono::seconds(30))) {
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return false;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

bool startService(const std::wstring& serviceName) {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return false;
    }

    const auto service = OpenServiceW(manager, serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status),
            sizeof(status),
            &bytesNeeded) &&
        status.dwCurrentState != SERVICE_RUNNING) {
        StartServiceW(service, 0, nullptr);
        if (!waitForState(service, SERVICE_RUNNING, std::chrono::seconds(30))) {
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return false;
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

bool extractArchive(const std::filesystem::path& archivePath, const std::filesystem::path& destinationPath) {
    std::wstring commandLine = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '"
        + archivePath.wstring()
        + L"' -DestinationPath '"
        + destinationPath.wstring()
        + L"' -Force\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    const auto created = CreateProcessW(
        nullptr,
        commandLine.data(),
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

bool copyExtractedFiles(const std::filesystem::path& sourceRoot, const std::filesystem::path& targetRoot) {
    std::error_code errorCode;
    std::filesystem::create_directories(targetRoot, errorCode);
    if (errorCode) {
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
        const auto relative = std::filesystem::relative(entry.path(), sourceRoot, errorCode);
        if (errorCode) {
            return false;
        }

        const auto targetPath = targetRoot / relative;
        if (entry.is_directory()) {
            std::filesystem::create_directories(targetPath, errorCode);
            if (errorCode) {
                return false;
            }
            continue;
        }

        std::filesystem::create_directories(targetPath.parent_path(), errorCode);
        if (errorCode) {
            return false;
        }

        std::filesystem::copy_file(entry.path(), targetPath, std::filesystem::copy_options::overwrite_existing, errorCode);
        if (errorCode) {
            return false;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const auto options = parseArgs(argc, argv);
    if (!options.has_value()) {
        std::cerr << "Usage: air_companion_updater --service-name <name> --package <zip> --target-dir <dir>\n";
        return 1;
    }

#ifdef _WIN32
    const auto serviceName = utf8ToWide(options->serviceName);
    if (serviceName.empty()) {
        return 1;
    }

    if (!stopService(serviceName)) {
        return 1;
    }

    const auto extractDirectory = std::filesystem::temp_directory_path() / "AIRCompanion" / "UpdateExtract";
    std::error_code errorCode;
    std::filesystem::remove_all(extractDirectory, errorCode);
    std::filesystem::create_directories(extractDirectory, errorCode);
    if (errorCode) {
        return 1;
    }

    if (!extractArchive(options->packagePath, extractDirectory)) {
        return 1;
    }

    auto sourceRoot = extractDirectory;
    const auto firstLevelEntries = [&extractDirectory] {
        std::vector<std::filesystem::path> entries;
        for (const auto& entry : std::filesystem::directory_iterator(extractDirectory)) {
            entries.push_back(entry.path());
        }
        return entries;
    }();
    if (firstLevelEntries.size() == 1 && std::filesystem::is_directory(firstLevelEntries.front())) {
        sourceRoot = firstLevelEntries.front();
    }

    if (!copyExtractedFiles(sourceRoot, options->targetDirectory)) {
        return 1;
    }

    return startService(serviceName) ? 0 : 1;
#else
    (void) options;
    return 1;
#endif
}
