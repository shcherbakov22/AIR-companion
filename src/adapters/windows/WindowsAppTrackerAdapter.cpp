#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <userenv.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) {
        return {};
    }

    return std::string(first, last);
}

#ifdef _WIN32
constexpr DWORD kDwmwaCloaked = 14;

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), converted.data(), size, nullptr, nullptr);
    return converted;
}

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

std::wstring readWindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const auto copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<std::size_t>(copied));
    return title;
}

std::string readProcessName(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }

    std::wstring path(4096, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);

    if (!ok || size == 0) {
        return {};
    }

    path.resize(static_cast<std::size_t>(size));
    const auto filename = std::filesystem::path(path).filename().wstring();
    return narrow(filename);
}

bool isWindowCloaked(HWND window) {
    BOOL cloaked = FALSE;
    return SUCCEEDED(DwmGetWindowAttribute(window, kDwmwaCloaked, &cloaked, sizeof(cloaked))) && cloaked != FALSE;
}

bool isExcludedProcessName(const std::string& processName) {
    static const std::unordered_set<std::string> excluded = {
        "textinputhost.exe",
        "searchhost.exe",
        "searchapp.exe",
        "shellexperiencehost.exe",
        "startmenuexperiencehost.exe",
        "lockapp.exe",
        "widgets.exe",
        "widgetboard.exe",
        "gamebar.exe",
        "applicationframehost.exe"
    };

    return excluded.contains(toLower(trim(processName)));
}

bool isAppWindow(HWND window, const std::string& processName) {
    if (!IsWindowVisible(window)) {
        return false;
    }

    if (IsIconic(window)) {
        return false;
    }

    if (window == GetShellWindow()) {
        return false;
    }

    const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    if ((exStyle & WS_EX_NOACTIVATE) != 0) {
        return false;
    }

    if (isWindowCloaked(window)) {
        return false;
    }

    const auto rootOwner = GetAncestor(window, GA_ROOTOWNER);
    if (rootOwner != nullptr && rootOwner != window) {
        return false;
    }

    const auto title = trim(narrow(readWindowTitle(window)));
    if (title.empty()) {
        return false;
    }

    if (isExcludedProcessName(processName)) {
        return false;
    }

    return true;
}

std::wstring currentExecutablePath() {
    std::wstring path(4096, L'\0');
    const auto copied = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (copied == 0) {
        return {};
    }

    path.resize(copied);
    return path;
}

std::wstring helperBinaryPath() {
    auto path = currentExecutablePath();
    if (path.empty()) {
        return {};
    }

    const std::wstring needle = L"air_companion_service.exe";
    const auto position = path.rfind(needle);
    if (position != std::wstring::npos) {
        path.replace(position, needle.size(), L"air_companion_helper.exe");
    }
    return path;
}

std::filesystem::path sharedInteractiveRuntimeDirectory() {
    wchar_t* publicDirectory = nullptr;
    std::wstring base = L"C:\\Users\\Public";
    if (size_t length = 0; _wdupenv_s(&publicDirectory, &length, L"PUBLIC") == 0 && publicDirectory != nullptr) {
        base.assign(publicDirectory);
        free(publicDirectory);
    }

    return std::filesystem::path(base) / "AIRCompanion" / "InteractiveCapture";
}

bool sameSessionAsActiveConsole() {
    DWORD processSessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &processSessionId)) {
        return false;
    }

    const auto activeSessionId = WTSGetActiveConsoleSessionId();
    return activeSessionId != 0xFFFFFFFF && processSessionId == activeSessionId;
}

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }

    return escaped;
}

std::optional<std::string> extractJsonString(const std::string& body, const std::string& key) {
    const auto keyPos = body.find("\"" + key + "\"");
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    const auto colonPos = body.find(':', keyPos);
    const auto quotePos = body.find('"', colonPos + 1);
    if (colonPos == std::string::npos || quotePos == std::string::npos) {
        return std::nullopt;
    }

    std::string value;
    for (std::size_t index = quotePos + 1; index < body.size(); ++index) {
        const auto ch = body[index];
        if (ch == '\\' && index + 1 < body.size()) {
            value += body[index + 1];
            ++index;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value += ch;
    }

    return std::nullopt;
}

companion::models::ActivitySnapshot parseSnapshotFile(const std::string& body) {
    companion::models::ActivitySnapshot snapshot;
    snapshot.focusedApp = extractJsonString(body, "focused_app").value_or({});
    snapshot.focusedWindowTitle = extractJsonString(body, "focused_window_title").value_or({});

    std::size_t searchStart = body.find("\"open_apps\"");
    if (searchStart == std::string::npos) {
        return snapshot;
    }

    searchStart = body.find('[', searchStart);
    if (searchStart == std::string::npos) {
        return snapshot;
    }

    auto cursor = searchStart;
    while ((cursor = body.find("\"app_name\"", cursor)) != std::string::npos) {
        const auto appName = extractJsonString(body.substr(cursor), "app_name").value_or({});
        const auto windowTitle = extractJsonString(body.substr(cursor), "window_title").value_or({});
        if (!appName.empty() || !windowTitle.empty()) {
            snapshot.openApps.push_back({
                .appName = appName,
                .windowTitle = windowTitle,
            });
        }
        ++cursor;
    }

    return snapshot;
}

struct WindowScanState {
    companion::models::ActivitySnapshot snapshot;
    std::unordered_set<std::string> seenEntries;
};

BOOL CALLBACK collectTopLevelWindows(HWND window, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowScanState*>(lParam);
    if (state == nullptr) {
        return TRUE;
    }

    const auto processName = trim(readProcessName(window));
    if (!isAppWindow(window, processName)) {
        return TRUE;
    }

    const auto windowTitle = trim(narrow(readWindowTitle(window)));
    if (processName.empty() && windowTitle.empty()) {
        return TRUE;
    }

    const auto dedupeKey = processName + "\n" + windowTitle;
    if (!state->seenEntries.insert(dedupeKey).second) {
        return TRUE;
    }

    state->snapshot.openApps.push_back({
        .appName = processName,
        .windowTitle = windowTitle,
    });

    return TRUE;
}

std::optional<std::wstring> readRegistryString(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ || size == 0) {
        return std::nullopt;
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(value.data()), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

void collectInstalledAppsFromUninstallKey(
    HKEY root,
    const wchar_t* path,
    const std::string& source,
    std::vector<companion::models::InstalledAppEntry>& apps
) {
    HKEY uninstallKey = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &uninstallKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
    while (RegEnumKeyExW(uninstallKey, index++, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY appKey = nullptr;
        if (RegOpenKeyExW(uninstallKey, subKeyName, 0, KEY_READ, &appKey) == ERROR_SUCCESS) {
            const auto displayName = readRegistryString(appKey, L"DisplayName");
            if (displayName.has_value() && !displayName->empty()) {
                companion::models::InstalledAppEntry entry;
                entry.displayName = narrow(*displayName);
                entry.appName = narrow(readRegistryString(appKey, L"DisplayIcon").value_or(*displayName));
                entry.displayVersion = narrow(readRegistryString(appKey, L"DisplayVersion").value_or(L""));
                entry.publisher = narrow(readRegistryString(appKey, L"Publisher").value_or(L""));
                entry.installLocation = narrow(readRegistryString(appKey, L"InstallLocation").value_or(L""));
                entry.source = source;
                apps.push_back(std::move(entry));
            }

            RegCloseKey(appKey);
        }

        subKeyNameSize = sizeof(subKeyName) / sizeof(wchar_t);
    }

    RegCloseKey(uninstallKey);
}
#endif

}  // namespace

namespace companion::adapters::windows {

models::ActivitySnapshot WindowsAppTrackerAdapter::snapshot() const {
#ifdef _WIN32
    if (sameSessionAsActiveConsole()) {
        return collectInteractiveSnapshot();
    }

    if (const auto helperSnapshot = captureViaActiveSessionHelper(); helperSnapshot.has_value()) {
        return *helperSnapshot;
    }
#endif

    return {};
}

std::vector<models::InstalledAppEntry> WindowsAppTrackerAdapter::installedApps() const {
#ifdef _WIN32
    std::vector<models::InstalledAppEntry> apps;
    collectInstalledAppsFromUninstallKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "registry_hklm", apps);
    collectInstalledAppsFromUninstallKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "registry_hklm_wow6432", apps);
    collectInstalledAppsFromUninstallKey(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", "registry_hkcu", apps);

    std::unordered_set<std::string> seen;
    std::vector<models::InstalledAppEntry> deduped;
    for (auto& app : apps) {
        const auto key = trim(app.displayName) + "\n" + trim(app.displayVersion) + "\n" + trim(app.publisher);
        if (!seen.insert(key).second) {
            continue;
        }
        if (trim(app.appName).empty()) {
            app.appName = app.displayName;
        }
        deduped.push_back(std::move(app));
    }

    std::sort(deduped.begin(), deduped.end(), [](const auto& left, const auto& right) {
        return left.displayName < right.displayName;
    });

    return deduped;
#else
    return {};
#endif
}

bool WindowsAppTrackerAdapter::writeSnapshotToFile(const std::string& outputPath) const {
#ifdef _WIN32
    const auto snapshot = collectInteractiveSnapshot();

    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
    std::ofstream output(outputPath, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "{"
           << "\"focused_app\":\"" << escapeJson(snapshot.focusedApp) << "\","
           << "\"focused_window_title\":\"" << escapeJson(snapshot.focusedWindowTitle) << "\","
           << "\"open_apps\":[";

    for (std::size_t index = 0; index < snapshot.openApps.size(); ++index) {
        if (index > 0) {
            output << ",";
        }

        output << "{"
               << "\"app_name\":\"" << escapeJson(snapshot.openApps[index].appName) << "\","
               << "\"window_title\":\"" << escapeJson(snapshot.openApps[index].windowTitle) << "\""
               << "}";
    }

    output << "]}";

    return output.good();
#else
    (void) outputPath;
    return false;
#endif
}

models::ActivitySnapshot WindowsAppTrackerAdapter::collectInteractiveSnapshot() const {
    models::ActivitySnapshot snapshot;

#ifdef _WIN32
    if (const auto foreground = GetForegroundWindow(); foreground != nullptr) {
        const auto foregroundProcessName = trim(readProcessName(foreground));
        if (isAppWindow(foreground, foregroundProcessName)) {
            snapshot.focusedApp = foregroundProcessName;
            snapshot.focusedWindowTitle = trim(narrow(readWindowTitle(foreground)));
        }
    }

    WindowScanState state;
    state.snapshot.focusedApp = snapshot.focusedApp;
    state.snapshot.focusedWindowTitle = snapshot.focusedWindowTitle;
    EnumWindows(collectTopLevelWindows, reinterpret_cast<LPARAM>(&state));
    snapshot.openApps = std::move(state.snapshot.openApps);

    if (!snapshot.focusedApp.empty() || !snapshot.focusedWindowTitle.empty()) {
        const auto found = std::find_if(
            snapshot.openApps.begin(),
            snapshot.openApps.end(),
            [&](const models::OpenAppEntry& entry) {
                return entry.appName == snapshot.focusedApp && entry.windowTitle == snapshot.focusedWindowTitle;
            });

        if (found == snapshot.openApps.end()) {
            snapshot.openApps.insert(snapshot.openApps.begin(), {
                .appName = snapshot.focusedApp,
                .windowTitle = snapshot.focusedWindowTitle,
            });
        }
    }
#endif

    return snapshot;
}

std::optional<models::ActivitySnapshot> WindowsAppTrackerAdapter::captureViaActiveSessionHelper() const {
#ifdef _WIN32
    const auto helperPath = helperBinaryPath();
    if (helperPath.empty() || !std::filesystem::exists(helperPath)) {
        return std::nullopt;
    }

    const auto activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId == 0xFFFFFFFF) {
        return std::nullopt;
    }

    auto outputPath = sharedInteractiveRuntimeDirectory() / "app-snapshot.json";
    std::error_code errorCode;
    std::filesystem::create_directories(outputPath.parent_path(), errorCode);
    std::filesystem::remove(outputPath, errorCode);

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(activeSessionId, &userToken)) {
        return std::nullopt;
    }

    HANDLE primaryToken = nullptr;
    if (!DuplicateTokenEx(userToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primaryToken)) {
        CloseHandle(userToken);
        return std::nullopt;
    }

    void* environment = nullptr;
    CreateEnvironmentBlock(&environment, primaryToken, FALSE);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION processInformation{};

    std::wstring commandLine = L"\"";
    commandLine += helperPath;
    commandLine += L"\" --snapshot-apps-once \"";
    commandLine += outputPath.wstring();
    commandLine += L"\"";

    const auto created = CreateProcessAsUserW(
        primaryToken,
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (environment != nullptr) {
        DestroyEnvironmentBlock(environment);
    }
    CloseHandle(primaryToken);
    CloseHandle(userToken);

    if (!created) {
        return std::nullopt;
    }

    WaitForSingleObject(processInformation.hProcess, 5000);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (std::filesystem::exists(outputPath)) {
            std::ifstream input(outputPath);
            if (input.is_open()) {
                const std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
                return parseSnapshotFile(body);
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#endif

    return std::nullopt;
}

}  // namespace companion::adapters::windows
