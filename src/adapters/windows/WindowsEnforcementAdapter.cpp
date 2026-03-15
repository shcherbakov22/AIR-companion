#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include <windows.h>
#include <tlhelp32.h>

namespace {

std::string normalizeProcessName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::unordered_set<std::string> normalizedBlockedApps(const std::vector<std::string>& blockedApps) {
    std::unordered_set<std::string> names;
    for (const auto& blockedApp : blockedApps) {
        if (!blockedApp.empty()) {
            names.insert(normalizeProcessName(blockedApp));
        }
    }
    return names;
}

void terminateProcessesByName(const std::unordered_set<std::string>& blockedApps) {
    if (blockedApps.empty()) {
        return;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }

    PROCESSENTRY32 processEntry{};
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &processEntry)) {
        CloseHandle(snapshot);
        return;
    }

    do {
        const auto name = normalizeProcessName(processEntry.szExeFile);
        if (blockedApps.find(name) == blockedApps.end()) {
            continue;
        }

        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, processEntry.th32ProcessID);
        if (process == nullptr) {
            continue;
        }

        TerminateProcess(process, 1);
        CloseHandle(process);
    } while (Process32Next(snapshot, &processEntry));

    CloseHandle(snapshot);
}

}  // namespace
#endif

namespace companion::adapters::windows {

void WindowsEnforcementAdapter::applyPolicy(const models::DevicePolicy& policy) {
    switch (policy.internetAccessMode) {
        case models::InternetAccessMode::AllowAll:
            m_lastState = "internet allow_all";
            break;
        case models::InternetAccessMode::BlockAll:
            m_lastState = "internet block_all";
            break;
        case models::InternetAccessMode::AllowListOnly:
            m_lastState = "internet allow_list_only";
            break;
    }

    if (policy.hasUnreadMentorChat || policy.hasUnreadAnnouncements) {
        m_lastState += " + communication gate";
    }

    if (policy.hasOpenViolations) {
        m_lastState += " + violations";
    }

    terminateBlockedApps(policy.blockedApps);
}

void WindowsEnforcementAdapter::terminateBlockedApps(const std::vector<std::string>& blockedApps) {
    if (blockedApps.empty()) {
        return;
    }

    m_lastState += " + blocked apps";

#ifdef _WIN32
    const auto normalized = normalizedBlockedApps(blockedApps);
    terminateProcessesByName(normalized);
#endif
}

std::string WindowsEnforcementAdapter::describeState() const {
    return m_lastState;
}

}  // namespace companion::adapters::windows

