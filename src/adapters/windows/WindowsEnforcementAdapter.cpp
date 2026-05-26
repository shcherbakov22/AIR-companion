#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/support/LocalLog.h"

#ifdef _WIN32
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <userenv.h>

namespace {

std::string normalizeProcessName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isProtectedProcessName(const std::string& processName) {
    static const std::unordered_set<std::string> protectedNames = {
        "explorer.exe",
        "rundll32.exe",
        "shellexperiencehost.exe",
        "startmenuexperiencehost.exe",
        "searchhost.exe",
        "searchapp.exe",
        "dwm.exe",
        "air_companion_tray.exe",
        "air_companion_service.exe",
    };

    return protectedNames.find(processName) != protectedNames.end();
}

bool isBrowserProcessName(const std::string& processName) {
    static const std::unordered_set<std::string> browserNames = {
        "chrome.exe",
        "msedge.exe",
        "firefox.exe",
        "brave.exe",
        "opera.exe",
        "opera_gx.exe",
        "iexplore.exe",
        "vivaldi.exe",
        "arc.exe",
    };

    return browserNames.find(processName) != browserNames.end();
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

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

std::wstring powerShellSingleQuoted(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (const wchar_t ch : value) {
        escaped.push_back(ch);
        if (ch == L'\'') {
            escaped.push_back(L'\'');
        }
    }
    return escaped;
}

std::wstring powerShellMessageScript() {
    return
        L"param([string]$Title,[string]$Body,[int]$DisplaySeconds);"
        L"$ErrorActionPreference='Stop';"
        L"Add-Type -AssemblyName System.Windows.Forms;"
        L"Add-Type -AssemblyName System.Drawing;"
        L"$null=$DisplaySeconds;"
        L"$form=New-Object System.Windows.Forms.Form;"
        L"$form.FormBorderStyle=[System.Windows.Forms.FormBorderStyle]::None;"
        L"$form.WindowState=[System.Windows.Forms.FormWindowState]::Maximized;"
        L"$form.TopMost=$true;"
        L"$form.BackColor=[System.Drawing.Color]::FromArgb(160,18,18);"
        L"$form.Bounds=[System.Windows.Forms.SystemInformation]::VirtualScreen;"
        L"$form.StartPosition=[System.Windows.Forms.FormStartPosition]::Manual;"
        L"$form.KeyPreview=$true;"
        L"$titleLabel=New-Object System.Windows.Forms.Label;"
        L"$titleLabel.Dock=[System.Windows.Forms.DockStyle]::Top;"
        L"$titleLabel.Height=180;"
        L"$titleLabel.ForeColor=[System.Drawing.Color]::White;"
        L"$titleLabel.Font=New-Object System.Drawing.Font('Segoe UI',36,[System.Drawing.FontStyle]::Bold);"
        L"$titleLabel.TextAlign=[System.Drawing.ContentAlignment]::MiddleCenter;"
        L"$titleLabel.Text=$Title;"
        L"$bodyLabel=New-Object System.Windows.Forms.Label;"
        L"$bodyLabel.Dock=[System.Windows.Forms.DockStyle]::Fill;"
        L"$bodyLabel.ForeColor=[System.Drawing.Color]::White;"
        L"$bodyLabel.Font=New-Object System.Drawing.Font('Segoe UI',28,[System.Drawing.FontStyle]::Regular);"
        L"$bodyLabel.TextAlign=[System.Drawing.ContentAlignment]::MiddleCenter;"
        L"$bodyLabel.Padding=New-Object System.Windows.Forms.Padding(120,40,120,80);"
        L"$bodyLabel.Text=$Body;"
        L"$bodyLabel.AutoEllipsis=$true;"
        L"$bottomPanel=New-Object System.Windows.Forms.Panel;"
        L"$bottomPanel.Dock=[System.Windows.Forms.DockStyle]::Bottom;"
        L"$bottomPanel.Height=180;"
        L"$bottomPanel.BackColor=$form.BackColor;"
        L"$okButton=New-Object System.Windows.Forms.Button;"
        L"$okButton.Text='OK';"
        L"$okButton.Width=220;"
        L"$okButton.Height=70;"
        L"$okButton.Font=New-Object System.Drawing.Font('Segoe UI',24,[System.Drawing.FontStyle]::Bold);"
        L"$okButton.ForeColor=[System.Drawing.Color]::FromArgb(120,15,15);"
        L"$okButton.BackColor=[System.Drawing.Color]::White;"
        L"$okButton.FlatStyle=[System.Windows.Forms.FlatStyle]::Flat;"
        L"$okButton.FlatAppearance.BorderSize=0;"
        L"$okButton.Add_Click({ $form.Close(); });"
        L"$footerLabel=New-Object System.Windows.Forms.Label;"
        L"$footerLabel.Height=60;"
        L"$footerLabel.Left=0;"
        L"$footerLabel.Top=95;"
        L"$footerLabel.Width=[System.Windows.Forms.SystemInformation]::VirtualScreen.Width;"
        L"$footerLabel.ForeColor=[System.Drawing.Color]::FromArgb(255,235,235);"
        L"$footerLabel.Font=New-Object System.Drawing.Font('Segoe UI',16,[System.Drawing.FontStyle]::Regular);"
        L"$footerLabel.TextAlign=[System.Drawing.ContentAlignment]::MiddleCenter;"
        L"$footerLabel.Text='Message from school system';"
        L"$bottomPanel.Controls.Add($okButton);"
        L"$bottomPanel.Controls.Add($footerLabel);"
        L"$bottomPanel.Add_Resize({ $okButton.Left=[Math]::Max(0,[int](($bottomPanel.ClientSize.Width - $okButton.Width)/2)); $okButton.Top=15; $footerLabel.Width=$bottomPanel.ClientSize.Width; });"
        L"$form.AcceptButton=$okButton;"
        L"$form.Controls.Add($bodyLabel);"
        L"$form.Controls.Add($bottomPanel);"
        L"$form.Controls.Add($titleLabel);"
        L"$form.Add_Shown({ $okButton.Left=[Math]::Max(0,[int](($bottomPanel.ClientSize.Width - $okButton.Width)/2)); $okButton.Top=15; $footerLabel.Width=$bottomPanel.ClientSize.Width; $okButton.Focus(); [console]::beep(880,300); Start-Sleep -Milliseconds 120; [console]::beep(880,300); });"
        L"$form.Add_KeyDown({ if ($_.KeyCode -eq [System.Windows.Forms.Keys]::Escape) { $form.Close(); } });"
        L"[System.Windows.Forms.Application]::Run($form);";
}

std::unordered_set<std::string> normalizedBlockedApps(const std::vector<std::string>& blockedApps) {
    std::unordered_set<std::string> names;
    for (const auto& blockedApp : blockedApps) {
        if (!blockedApp.empty()) {
            const auto normalized = normalizeProcessName(blockedApp);
            if (!isProtectedProcessName(normalized)) {
                names.insert(normalized);
            }
        }
    }
    return names;
}

std::string lastErrorText() {
    return std::to_string(GetLastError());
}

std::vector<std::string> terminateProcessesByName(
    const std::unordered_set<std::string>& blockedApps,
    const std::string& reason
) {
    std::vector<std::string> failures;
    if (blockedApps.empty()) {
        companion::support::appendDebugLog("app close skipped: no targets reason=" + reason);
        return failures;
    }

    companion::support::appendDebugLog(
        "app close scan started reason=" + reason
        + " target_count=" + std::to_string(blockedApps.size())
    );

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        companion::support::appendDebugLog("app close scan failed: process snapshot unavailable reason=" + reason + " error=" + lastErrorText());
        failures.emplace_back("process snapshot unavailable");
        return failures;
    }

    PROCESSENTRY32 processEntry{};
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &processEntry)) {
        companion::support::appendDebugLog("app close scan failed: process snapshot empty reason=" + reason + " error=" + lastErrorText());
        CloseHandle(snapshot);
        failures.emplace_back("process snapshot empty");
        return failures;
    }

    do {
        const auto name = normalizeProcessName(processEntry.szExeFile);
        if (blockedApps.find(name) == blockedApps.end()) {
            continue;
        }

        if (isProtectedProcessName(name)) {
            companion::support::appendDebugLog("app close skipped protected process=" + name + " pid=" + std::to_string(processEntry.th32ProcessID) + " reason=" + reason);
            continue;
        }

        companion::support::appendDebugLog("app close attempt process=" + name + " pid=" + std::to_string(processEntry.th32ProcessID) + " reason=" + reason);
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, processEntry.th32ProcessID);
        if (process == nullptr) {
            companion::support::appendDebugLog("app close failed open_process process=" + name + " pid=" + std::to_string(processEntry.th32ProcessID) + " reason=" + reason + " error=" + lastErrorText());
            failures.push_back(name);
            continue;
        }

        if (!TerminateProcess(process, 1)) {
            companion::support::appendDebugLog("app close failed terminate process=" + name + " pid=" + std::to_string(processEntry.th32ProcessID) + " reason=" + reason + " error=" + lastErrorText());
            failures.push_back(name);
        } else {
            companion::support::appendDebugLog("app close success process=" + name + " pid=" + std::to_string(processEntry.th32ProcessID) + " reason=" + reason);
        }
        CloseHandle(process);
    } while (Process32Next(snapshot, &processEntry));

    CloseHandle(snapshot);

    std::sort(failures.begin(), failures.end());
    failures.erase(std::unique(failures.begin(), failures.end()), failures.end());
    companion::support::appendDebugLog(
        "app close scan finished reason=" + reason
        + " failure_count=" + std::to_string(failures.size())
    );
    return failures;
}

}  // namespace
#endif

namespace companion::adapters::windows {

std::unordered_set<std::string> WindowsEnforcementAdapter::violationKillTargets(
    const models::DevicePolicy& policy,
    const models::ActivitySnapshot& snapshot
) {
    std::unordered_set<std::string> normalizedOpenApps;
    for (const auto& app : snapshot.openApps) {
        if (app.appName.empty()) {
            continue;
        }

        const auto normalized = normalizeProcessName(app.appName);
        if (!normalized.empty()) {
            normalizedOpenApps.insert(normalized);
        }
    }

    if (!policy.violationAppEnforcement.killGuiApps) {
        m_lastObservedOpenApps = std::move(normalizedOpenApps);
        m_browserGraceUntil.clear();
        return {};
    }

    const auto now = std::chrono::steady_clock::now();
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> nextBrowserGraceUntil;
    std::unordered_set<std::string> targets;

    for (const auto& processName : normalizedOpenApps) {
        if (isProtectedProcessName(processName)) {
            continue;
        }

        if (!isBrowserProcessName(processName)) {
            targets.insert(processName);
            continue;
        }

        auto graceUntil = m_browserGraceUntil.find(processName);
        const bool justReopened = m_lastObservedOpenApps.find(processName) == m_lastObservedOpenApps.end();
        if (justReopened) {
            graceUntil = nextBrowserGraceUntil.emplace(
                processName,
                now + std::chrono::seconds(std::max(policy.violationAppEnforcement.browserReopenGraceSeconds, 0))
            ).first;
        } else if (graceUntil != m_browserGraceUntil.end()) {
            nextBrowserGraceUntil.emplace(processName, graceUntil->second);
            graceUntil = nextBrowserGraceUntil.find(processName);
        }

        if (graceUntil == nextBrowserGraceUntil.end() || graceUntil->second <= now) {
            targets.insert(processName);
        }
    }

    m_lastObservedOpenApps = std::move(normalizedOpenApps);
    m_browserGraceUntil = std::move(nextBrowserGraceUntil);

    return targets;
}

void WindowsEnforcementAdapter::applyPolicy(const models::DevicePolicy& policy, const models::ActivitySnapshot& snapshot) {
    m_lastState = "policy applied";
    companion::support::appendDebugLog(
        "enforcement policy applied open_violations=" + std::string(policy.hasOpenViolations ? "true" : "false")
        + " kill_gui_apps=" + std::string(policy.violationAppEnforcement.killGuiApps ? "true" : "false")
        + " blocked_apps=" + std::to_string(policy.blockedApps.size())
        + " browser_tracking=" + std::string(policy.browserTrackingEnabled ? "enabled" : "disabled")
        + " focused_app=" + snapshot.focusedApp
        + " active_domain=" + snapshot.activeBrowserDomain
    );

    if (policy.hasUnreadMentorChat || policy.hasUnreadAnnouncements) {
        m_lastState += " + communication gate";
    }

    if (policy.hasOpenViolations) {
        m_lastState += " + violations";
    }

    const auto violationTargets = violationKillTargets(policy, snapshot);
    if (!violationTargets.empty()) {
        m_lastState += " + stale violation gui kill";
#ifdef _WIN32
        (void) terminateProcessesByName(violationTargets, "open_violation_close_all_apps");
#endif
    }
}

std::vector<std::string> WindowsEnforcementAdapter::terminateBlockedApps(const std::vector<std::string>& blockedApps) {
    if (blockedApps.empty()) {
        return {};
    }

    m_lastState += " + blocked apps";

#ifdef _WIN32
    const auto normalized = normalizedBlockedApps(blockedApps);
    companion::support::appendDebugLog(
        "blocked app enforcement requested configured_count=" + std::to_string(blockedApps.size())
        + " normalized_count=" + std::to_string(normalized.size())
    );
    return terminateProcessesByName(normalized, "blocked_program_policy");
#else
    return {};
#endif
}

bool WindowsEnforcementAdapter::showMessage(
    const std::string& title,
    const std::string& body,
    int displaySeconds,
    std::string& error
) {
#ifndef _WIN32
    (void) title;
    (void) body;
    (void) displaySeconds;
    error = "message display unsupported on this platform";
    return false;
#else
    const DWORD activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId == 0xFFFFFFFF) {
        error = "no interactive session is active";
        return false;
    }

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(activeSessionId, &userToken)) {
        error = "WTSQueryUserToken failed error=" + std::to_string(GetLastError());
        return false;
    }

    HANDLE primaryToken = nullptr;
    if (!DuplicateTokenEx(userToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primaryToken)) {
        error = "DuplicateTokenEx failed error=" + std::to_string(GetLastError());
        CloseHandle(userToken);
        return false;
    }
    CloseHandle(userToken);

    const auto baseDirectory = std::filesystem::path("C:\\ProgramData\\AIRCompanion\\Internal");
    std::error_code filesystemError;
    std::filesystem::create_directories(baseDirectory, filesystemError);
    if (filesystemError) {
        error = "unable to create message helper directory";
        CloseHandle(primaryToken);
        return false;
    }

    const auto scriptPath = baseDirectory / "show-message.ps1";
    std::ofstream scriptOutput(scriptPath, std::ios::binary | std::ios::trunc);
    if (!scriptOutput.is_open()) {
        error = "unable to write message helper script";
        CloseHandle(primaryToken);
        return false;
    }
    scriptOutput << wideToUtf8(powerShellMessageScript());
    scriptOutput.close();

    const auto wideScriptPath = scriptPath.wstring();
    const auto wideTitle = utf8ToWide(title);
    const auto wideBody = utf8ToWide(body);
    if (wideScriptPath.empty() || wideTitle.empty() || wideBody.empty()) {
        error = "invalid message payload";
        CloseHandle(primaryToken);
        return false;
    }

    std::wstring commandLine =
        L"powershell.exe -Sta -NoProfile -ExecutionPolicy Bypass -File \"";
    commandLine += wideScriptPath;
    commandLine += L"\" -Title '";
    commandLine += powerShellSingleQuoted(wideTitle);
    commandLine += L"' -Body '";
    commandLine += powerShellSingleQuoted(wideBody);
    commandLine += L"' -DisplaySeconds ";
    commandLine += std::to_wstring(std::max(5, std::min(displaySeconds, 120)));

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION processInformation{};

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    void* environment = nullptr;
    CreateEnvironmentBlock(&environment, primaryToken, FALSE);

    const BOOL created = CreateProcessAsUserW(
        primaryToken,
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        environment,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (environment != nullptr) {
        DestroyEnvironmentBlock(environment);
    }
    CloseHandle(primaryToken);

    if (!created) {
        error = "CreateProcessAsUserW failed error=" + std::to_string(GetLastError());
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, 1500);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    if (waitResult == WAIT_FAILED) {
        error = "message window wait failed error=" + std::to_string(GetLastError());
        return false;
    }

    if (waitResult == WAIT_OBJECT_0 && exitCode != STILL_ACTIVE && exitCode != 0) {
        error = "message window exited code=" + std::to_string(exitCode);
        return false;
    }

    m_lastState = "message shown";
    return true;
#endif
}

std::string WindowsEnforcementAdapter::describeState() const {
    return m_lastState;
}

}  // namespace companion::adapters::windows

