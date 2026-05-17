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

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
    result.resize(static_cast<std::size_t>(required - 1));
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

bool configureServiceRecovery(const std::wstring& serviceName) {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return false;
    }

    const auto service = OpenServiceW(manager, serviceName.c_str(), SERVICE_CHANGE_CONFIG);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return false;
    }

    SC_ACTION actions[3]{};
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 15000;
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000;

    SERVICE_FAILURE_ACTIONSW failureActions{};
    failureActions.dwResetPeriod = 86400;
    failureActions.cActions = 3;
    failureActions.lpsaActions = actions;

    SERVICE_DELAYED_AUTO_START_INFO delayedAutoStart{};
    delayedAutoStart.fDelayedAutostart = TRUE;

    const bool ok = ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions) != FALSE
        && ChangeServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayedAutoStart) != FALSE;

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok;
}

bool runHiddenAndWait(std::wstring commandLine, DWORD timeoutMs) {
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

    const auto waitResult = WaitForSingleObject(processInformation.hProcess, timeoutMs);
    DWORD exitCode = 1;
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(processInformation.hProcess, 1);
    } else {
        GetExitCodeProcess(processInformation.hProcess, &exitCode);
    }

    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return waitResult != WAIT_TIMEOUT && exitCode == 0;
}

void stopCompanionUserProcesses() {
    runHiddenAndWait(L"taskkill.exe /IM air_companion_tray.exe /F /T", 10000);
    runHiddenAndWait(L"taskkill.exe /IM air_companion_helper.exe /F /T", 10000);
}

bool ensureWatchdogTask(const std::wstring& taskName,
                        const std::wstring& schedule,
                        const std::wstring& serviceName) {
    std::wstring commandLine = L"schtasks.exe /Create /TN \""
        + taskName
        + L"\" "
        + schedule
        + L" /RU SYSTEM /RL HIGHEST /TR \"cmd.exe /c sc start "
        + serviceName
        + L"\" /F";

    return runHiddenAndWait(std::move(commandLine), 30000);
}

bool ensureWatchdogTasks(const std::wstring& serviceName) {
    const bool boot = ensureWatchdogTask(
        L"AIR Companion Service (Boot)",
        L"/SC ONSTART",
        serviceName
    );
    const bool logon = ensureWatchdogTask(
        L"AIR Companion Service (Logon)",
        L"/SC ONLOGON",
        serviceName
    );
    const bool watchdog = ensureWatchdogTask(
        L"AIR Companion Service (Watchdog)",
        L"/SC MINUTE /MO 1",
        serviceName
    );

    return boot && logon && watchdog;
}

enum class UsersAccess {
    None,
    ReadExecute,
    Modify,
};

bool protectPath(const std::filesystem::path& path, UsersAccess usersAccess) {
    std::error_code errorCode;
    std::filesystem::create_directories(path, errorCode);
    if (errorCode) {
        return false;
    }

    std::wstring commandLine = L"icacls.exe \""
        + path.wstring()
        + L"\" /inheritance:r /grant:r \"*S-1-5-18:(OI)(CI)F\" \"*S-1-5-32-544:(OI)(CI)F\"";

    if (usersAccess == UsersAccess::ReadExecute) {
        commandLine += L" \"*S-1-5-32-545:(OI)(CI)RX\"";
    } else if (usersAccess == UsersAccess::Modify) {
        commandLine += L" \"*S-1-5-32-545:(OI)(CI)M\"";
    }

    commandLine += L" /T /C";

    return runHiddenAndWait(std::move(commandLine), 120000);
}

bool protectCompanionStorage(const std::filesystem::path& targetDirectory) {
    const auto programData = [] {
        const auto* value = std::getenv("PROGRAMDATA");
        return value != nullptr && *value != '\0'
            ? std::filesystem::path(value)
            : std::filesystem::path("C:\\ProgramData");
    }();
    const auto publicDirectory = [] {
        const auto* value = std::getenv("PUBLIC");
        return value != nullptr && *value != '\0'
            ? std::filesystem::path(value)
            : std::filesystem::path("C:\\Users\\Public");
    }();

    const bool installDirectoryOk = protectPath(targetDirectory, UsersAccess::ReadExecute);
    const bool serviceConfigOk = protectPath(programData / "AIRCompanion" / "Service", UsersAccess::None);
    const bool internalConfigOk = protectPath(programData / "AIRCompanion" / "Internal", UsersAccess::None);
    const bool interactiveRuntimeOk = protectPath(
        publicDirectory / "AIRCompanion" / "InteractiveCapture",
        UsersAccess::Modify
    );

    return installDirectoryOk && serviceConfigOk && internalConfigOk && interactiveRuntimeOk;
}

bool extractArchive(const std::filesystem::path& archivePath, const std::filesystem::path& destinationPath) {
    std::wstring commandLine = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '"
        + archivePath.wstring()
        + L"' -DestinationPath '"
        + destinationPath.wstring()
        + L"' -Force\"";

    return runHiddenAndWait(std::move(commandLine), 120000);
}
#endif

bool copyFileWithRetry(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath) {
    std::error_code errorCode;
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, errorCode);
        if (!errorCode) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        errorCode.clear();
    }

    return false;
}

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

        if (!copyFileWithRetry(entry.path(), targetPath)) {
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
    stopCompanionUserProcesses();

    const auto extractDirectory = std::filesystem::temp_directory_path() / "AIRCompanion" / "UpdateExtract";
    std::error_code errorCode;
    std::filesystem::remove_all(extractDirectory, errorCode);
    std::filesystem::create_directories(extractDirectory, errorCode);
    if (errorCode) {
        startService(serviceName);
        return 1;
    }

    if (!extractArchive(options->packagePath, extractDirectory)) {
        startService(serviceName);
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
        startService(serviceName);
        return 1;
    }

    if (!configureServiceRecovery(serviceName)
        || !ensureWatchdogTasks(serviceName)
        || !protectCompanionStorage(options->targetDirectory)) {
        startService(serviceName);
        return 1;
    }

    return startService(serviceName) ? 0 : 1;
#else
    (void) options;
    return 1;
#endif
}
