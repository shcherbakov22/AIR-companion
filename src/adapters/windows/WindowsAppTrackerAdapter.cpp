#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include <windows.h>

namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) {
        return {};
    }

    return std::string(first, last);
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

bool shouldTrackWindow(HWND window) {
    if (!IsWindowVisible(window)) {
        return false;
    }

    if (GetWindow(window, GW_OWNER) != nullptr) {
        return false;
    }

    const auto title = trim(narrow(readWindowTitle(window)));
    if (title.empty()) {
        return false;
    }

    return true;
}

struct WindowScanState {
    std::vector<std::string> openApps;
    std::unordered_set<std::string> seenApps;
};

BOOL CALLBACK collectTopLevelWindows(HWND window, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowScanState*>(lParam);
    if (state == nullptr || !shouldTrackWindow(window)) {
        return TRUE;
    }

    auto processName = trim(readProcessName(window));
    if (processName.empty()) {
        return TRUE;
    }

    if (state->seenApps.insert(processName).second) {
        state->openApps.push_back(std::move(processName));
    }

    return TRUE;
}

}  // namespace
#endif

namespace companion::adapters::windows {

models::ActivitySnapshot WindowsAppTrackerAdapter::snapshot() const {
    models::ActivitySnapshot snapshot;

#ifdef _WIN32
    if (const auto foreground = GetForegroundWindow(); foreground != nullptr) {
        snapshot.focusedApp = trim(readProcessName(foreground));
        snapshot.focusedWindowTitle = trim(narrow(readWindowTitle(foreground)));
    }

    WindowScanState state;
    EnumWindows(collectTopLevelWindows, reinterpret_cast<LPARAM>(&state));
    snapshot.openApps = std::move(state.openApps);

    if (!snapshot.focusedApp.empty()) {
        const auto found = std::find(snapshot.openApps.begin(), snapshot.openApps.end(), snapshot.focusedApp);
        if (found == snapshot.openApps.end()) {
            snapshot.openApps.insert(snapshot.openApps.begin(), snapshot.focusedApp);
        }
    }
#endif

    return snapshot;
}

}  // namespace companion::adapters::windows

