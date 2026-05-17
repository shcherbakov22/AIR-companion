#include "companion/service/BootAutoStartRegistrar.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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
    const auto xmlPath = std::filesystem::temp_directory_path() / "air-companion-startup-task.xml";
    {
        std::ofstream output(xmlPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        const auto taskXml = utf8ToWide(taskXmlForServiceBinary(servicePath));
        constexpr unsigned char bom[] = {0xFF, 0xFE};
        output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        output.write(
            reinterpret_cast<const char*>(taskXml.data()),
            static_cast<std::streamsize>(taskXml.size() * sizeof(wchar_t))
        );
    }

    const auto wideXmlPath = utf8ToWide(xmlPath.string());
    const std::wstring commandLine =
        L"schtasks.exe /Create /TN \"" + wideTaskName
        + L"\" /XML \"" + wideXmlPath + L"\" /F";

    const auto created = runProcess(commandLine);
    std::error_code errorCode;
    std::filesystem::remove(xmlPath, errorCode);
    return created;
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

std::string BootAutoStartRegistrar::taskXmlForServiceBinary(const std::string& serviceBinaryPath) {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
        "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
        "  <RegistrationInfo>\n"
        "    <Author>AIR Companion</Author>\n"
        "    <URI>\\" + taskName() + "</URI>\n"
        "  </RegistrationInfo>\n"
        "  <Principals>\n"
        "    <Principal id=\"Author\">\n"
        "      <UserId>S-1-5-18</UserId>\n"
        "      <RunLevel>HighestAvailable</RunLevel>\n"
        "    </Principal>\n"
        "  </Principals>\n"
        "  <Settings>\n"
        "    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
        "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
        "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
        "    <AllowHardTerminate>true</AllowHardTerminate>\n"
        "    <StartWhenAvailable>true</StartWhenAvailable>\n"
        "    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n"
        "    <AllowStartOnDemand>true</AllowStartOnDemand>\n"
        "    <Enabled>true</Enabled>\n"
        "    <Hidden>true</Hidden>\n"
        "    <RunOnlyIfIdle>false</RunOnlyIfIdle>\n"
        "    <WakeToRun>false</WakeToRun>\n"
        "    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
        "    <Priority>5</Priority>\n"
        "    <RestartOnFailure>\n"
        "      <Interval>PT1M</Interval>\n"
        "      <Count>3</Count>\n"
        "    </RestartOnFailure>\n"
        "  </Settings>\n"
        "  <Triggers>\n"
        "    <BootTrigger>\n"
        "      <Enabled>true</Enabled>\n"
        "    </BootTrigger>\n"
        "    <EventTrigger>\n"
        "      <Enabled>true</Enabled>\n"
        "      <Subscription>&lt;QueryList&gt;&lt;Query Id=\"0\" Path=\"System\"&gt;&lt;Select Path=\"System\"&gt;*[System[Provider[@Name='Power-Troubleshooter'] and (EventID=1)]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>\n"
        "    </EventTrigger>\n"
        "  </Triggers>\n"
        "  <Actions Context=\"Author\">\n"
        "    <Exec>\n"
        "      <Command>" + serviceBinaryPath + "</Command>\n"
        "    </Exec>\n"
        "  </Actions>\n"
        "</Task>\n";
}

}  // namespace companion::service
