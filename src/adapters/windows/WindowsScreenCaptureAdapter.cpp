#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace companion::adapters::windows {

#ifdef _WIN32
namespace {
constexpr int kOutputWidth = 1493;
constexpr int kOutputHeight = 840;
constexpr ULONG kJpegQuality = 88;

void appendDebugLog(const std::string& line) {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr || *appData == '\0') {
        return;
    }

    const auto logDirectory = std::filesystem::path(appData) / "AIRCompanion";
    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::ofstream output(logDirectory / "debug.log", std::ios::app);
    if (!output.is_open()) {
        return;
    }

    output << line << '\n';
#else
    (void) line;
#endif
}

class GdiPlusSession {
public:
    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput startupInput;
        Gdiplus::GdiplusStartup(&m_token, &startupInput, nullptr);
    }

    ~GdiPlusSession() {
        if (m_token != 0) {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

private:
    ULONG_PTR m_token{0};
};

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
    for (const wchar_t character : value) {
        escaped.push_back(character);
        if (character == L'\'') {
            escaped.push_back(L'\'');
        }
    }
    return escaped;
}

std::wstring powerShellCaptureScript() {
    std::wstringstream script;
    script
        << L"param([string]$OutputPath);"
        << L"$ErrorActionPreference='Stop';"
        << L"Add-Type -AssemblyName System.Windows.Forms;"
        << L"Add-Type -AssemblyName System.Drawing;"
        << L"$bounds=[System.Windows.Forms.SystemInformation]::VirtualScreen;"
        << L"if($bounds.Width -le 0 -or $bounds.Height -le 0){throw 'invalid virtual screen bounds'};"
        << L"$capture=[System.Drawing.Bitmap]::new($bounds.Width,$bounds.Height);"
        << L"$graphics=[System.Drawing.Graphics]::FromImage($capture);"
        << L"$graphics.CopyFromScreen($bounds.Left,$bounds.Top,0,0,$capture.Size);"
        << L"$output=[System.Drawing.Bitmap]::new(1493,840);"
        << L"$outputGraphics=[System.Drawing.Graphics]::FromImage($output);"
        << L"$outputGraphics.Clear([System.Drawing.Color]::Black);"
        << L"$scale=[Math]::Min(1493.0 / $bounds.Width, 840.0 / $bounds.Height);"
        << L"$drawWidth=[int]($bounds.Width * $scale);"
        << L"$drawHeight=[int]($bounds.Height * $scale);"
        << L"$offsetX=[int]((1493 - $drawWidth) / 2);"
        << L"$offsetY=[int]((840 - $drawHeight) / 2);"
        << L"$dest=[System.Drawing.Rectangle]::new($offsetX,$offsetY,$drawWidth,$drawHeight);"
        << L"$outputGraphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic;"
        << L"$outputGraphics.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality;"
        << L"$outputGraphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::HighQuality;"
        << L"$outputGraphics.DrawImage($capture,$dest);"
        << L"$directory=[System.IO.Path]::GetDirectoryName($OutputPath);"
        << L"if(-not [string]::IsNullOrWhiteSpace($directory)){[System.IO.Directory]::CreateDirectory($directory) | Out-Null;}"
        << L"$output.Save($OutputPath,[System.Drawing.Imaging.ImageFormat]::Jpeg);"
        << L"$outputGraphics.Dispose();"
        << L"$graphics.Dispose();"
        << L"$output.Dispose();"
        << L"$capture.Dispose();";
    return script.str();
}

std::optional<std::string> runPowerShellScreenCapture(const std::string& outputPath) {
    const auto wideOutputPath = utf8ToWide(outputPath);
    if (wideOutputPath.empty()) {
        appendDebugLog("screen capture powershell invalid output path");
        return std::nullopt;
    }

    const std::wstring commandLine =
        L"powershell.exe -Sta -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"" + powerShellCaptureScript() + L"\" -OutputPath '" + powerShellSingleQuoted(wideOutputPath) + L"'";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    const BOOL created = CreateProcessW(
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
        appendDebugLog("screen capture powershell CreateProcessW failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }

    const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, 30000);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    if (waitResult == WAIT_FAILED) {
        appendDebugLog("screen capture powershell wait failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }

    if (waitResult == WAIT_TIMEOUT) {
        appendDebugLog("screen capture powershell timed out");
        return std::nullopt;
    }

    if (exitCode != 0) {
        appendDebugLog("screen capture powershell exitCode=" + std::to_string(exitCode));
        return std::nullopt;
    }

    if (!std::filesystem::exists(outputPath)) {
        appendDebugLog("screen capture powershell missing output path=" + outputPath);
        return std::nullopt;
    }

    return outputPath;
}

std::optional<std::string> runPowerShellScreenCaptureAsUser(
    HANDLE primaryToken,
    const std::filesystem::path& scriptPath,
    const std::filesystem::path& outputPath
) {
    const auto wideScriptPath = scriptPath.wstring();
    const auto wideOutputPath = outputPath.wstring();
    if (wideScriptPath.empty() || wideOutputPath.empty()) {
        appendDebugLog("screen capture helper invalid powershell script or output path");
        return std::nullopt;
    }

    std::wstring commandLine =
        L"powershell.exe -Sta -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"";
    commandLine += wideScriptPath;
    commandLine += L"\" -OutputPath \"";
    commandLine += wideOutputPath;
    commandLine += L"\"";

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
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment,
        nullptr,
        &startupInfo,
        &processInformation
    );

    if (environment != nullptr) {
        DestroyEnvironmentBlock(environment);
    }

    if (!created) {
        appendDebugLog("screen capture powershell CreateProcessAsUserW failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }

    const DWORD waitResult = WaitForSingleObject(processInformation.hProcess, 30000);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    if (waitResult == WAIT_FAILED) {
        appendDebugLog("screen capture powershell wait failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }

    if (waitResult == WAIT_TIMEOUT) {
        appendDebugLog("screen capture powershell timed out");
        return std::nullopt;
    }

    if (exitCode != 0) {
        appendDebugLog("screen capture powershell exitCode=" + std::to_string(exitCode));
        return std::nullopt;
    }

    if (!std::filesystem::exists(outputPath)) {
        appendDebugLog("screen capture powershell missing output path=" + wideToUtf8(outputPath.wstring()));
        return std::nullopt;
    }

    return wideToUtf8(outputPath.wstring());
}

bool encoderClsid(const wchar_t* mimeType, CLSID& clsid) {
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok || encoderBytes == 0) {
        return false;
    }

    std::vector<std::byte> buffer(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return false;
    }

    for (UINT index = 0; index < encoderCount; ++index) {
        if (wcscmp(encoders[index].MimeType, mimeType) == 0) {
            clsid = encoders[index].Clsid;
            return true;
        }
    }

    return false;
}

Gdiplus::Rect fitRect(INT sourceWidth, INT sourceHeight) {
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return {0, 0, kOutputWidth, kOutputHeight};
    }

    const double scaleX = static_cast<double>(kOutputWidth) / static_cast<double>(sourceWidth);
    const double scaleY = static_cast<double>(kOutputHeight) / static_cast<double>(sourceHeight);
    const double scale = scaleX < scaleY ? scaleX : scaleY;

    const INT drawWidth = static_cast<INT>(sourceWidth * scale);
    const INT drawHeight = static_cast<INT>(sourceHeight * scale);
    const INT offsetX = (kOutputWidth - drawWidth) / 2;
    const INT offsetY = (kOutputHeight - drawHeight) / 2;

    return {offsetX, offsetY, drawWidth, drawHeight};
}

std::optional<std::string> saveBitmapAsJpeg(HBITMAP bitmap, INT width, INT height, const std::string& outputPath) {
    CLSID encoder{};
    if (!encoderClsid(L"image/jpeg", encoder)) {
        appendDebugLog("screen capture jpeg encoder lookup failed");
        return std::nullopt;
    }

    Gdiplus::Bitmap source(bitmap, nullptr);
    Gdiplus::Bitmap output(kOutputWidth, kOutputHeight, PixelFormat24bppRGB);
    Gdiplus::Graphics graphics(&output);
    graphics.Clear(Gdiplus::Color(0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    const auto targetRect = fitRect(width, height);
    graphics.DrawImage(&source, targetRect);

    Gdiplus::EncoderParameters encoderParameters{};
    encoderParameters.Count = 1;
    encoderParameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoderParameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoderParameters.Parameter[0].NumberOfValues = 1;
    encoderParameters.Parameter[0].Value = const_cast<ULONG*>(&kJpegQuality);

    const auto widePath = utf8ToWide(outputPath);
    if (output.Save(widePath.c_str(), &encoder, &encoderParameters) != Gdiplus::Ok) {
        appendDebugLog("screen capture jpeg save failed path=" + outputPath);
        return std::nullopt;
    }

    return outputPath;
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

std::filesystem::path sharedInteractiveCaptureDirectory() {
    if (const auto* programData = std::getenv("PROGRAMDATA"); programData != nullptr && *programData != '\0') {
        return std::filesystem::path(programData) / "AIRCompanion" / "Internal";
    }

    return std::filesystem::path("C:\\ProgramData\\AIRCompanion\\Internal");
}

bool sameSessionAsActiveConsole() {
    DWORD processSessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &processSessionId)) {
        return false;
    }

    const auto activeSessionId = WTSGetActiveConsoleSessionId();
    return activeSessionId != 0xFFFFFFFF && processSessionId == activeSessionId;
}

}  // namespace
#endif

std::optional<std::string> WindowsScreenCaptureAdapter::captureToFile(const std::string& outputDirectory) {
#ifdef _WIN32
    if (sameSessionAsActiveConsole()) {
        appendDebugLog("screen capture using interactive path output=" + outputDirectory);
        return captureInteractive(outputDirectory);
    }

    appendDebugLog("screen capture using active-session helper output=" + outputDirectory);
    return captureViaActiveSessionHelper(outputDirectory);
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

std::optional<std::string> WindowsScreenCaptureAdapter::captureInteractive(const std::string& outputDirectory) const {
    std::filesystem::create_directories(outputDirectory);

#ifdef _WIN32
    const auto path = outputDirectory + "/screen-capture.jpg";
    appendDebugLog("screen capture interactive using powershell path=" + path);
    const auto result = runPowerShellScreenCapture(path);
    if (result.has_value()) {
        appendDebugLog("screen capture interactive saved path=" + path);
    }
    return result;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

std::optional<std::string> WindowsScreenCaptureAdapter::captureViaActiveSessionHelper(const std::string& outputDirectory) const {
#ifdef _WIN32
    const auto stagingDirectory = sharedInteractiveCaptureDirectory();
    std::filesystem::create_directories(stagingDirectory);
    const auto stagingPath = stagingDirectory / "screen-capture.jpg";
    const auto scriptPath = stagingDirectory / "screen-capture.ps1";
    const auto finalOutputPath = std::filesystem::path(outputDirectory) / "screen-capture.jpg";
    std::error_code errorCode;
    std::filesystem::remove(stagingPath, errorCode);
    std::filesystem::create_directories(finalOutputPath.parent_path(), errorCode);
    std::filesystem::remove(finalOutputPath, errorCode);
    appendDebugLog("screen capture helper staging=" + wideToUtf8(stagingPath.wstring()));

    std::ofstream scriptOutput(scriptPath, std::ios::trunc);
    if (!scriptOutput.is_open()) {
        appendDebugLog("screen capture helper unable to write powershell script");
        return std::nullopt;
    }
    const auto scriptBody = powerShellCaptureScript();
    const auto scriptText = wideToUtf8(scriptBody);
    scriptOutput << scriptText;
    scriptOutput.close();

    const auto activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId == 0xFFFFFFFF) {
        appendDebugLog("screen capture helper no active console session");
        return std::nullopt;
    }
    appendDebugLog("screen capture helper activeSessionId=" + std::to_string(activeSessionId));

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(activeSessionId, &userToken)) {
        appendDebugLog("screen capture helper WTSQueryUserToken failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }

    HANDLE primaryToken = nullptr;
    if (!DuplicateTokenEx(userToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primaryToken)) {
        appendDebugLog("screen capture helper DuplicateTokenEx failed error=" + std::to_string(GetLastError()));
        CloseHandle(userToken);
        return std::nullopt;
    }

    appendDebugLog("screen capture helper launching powershell directly");
    const auto captureResult = runPowerShellScreenCaptureAsUser(primaryToken, scriptPath, stagingPath);
    CloseHandle(primaryToken);
    CloseHandle(userToken);
    if (!captureResult.has_value()) {
        return std::nullopt;
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
        if (std::filesystem::exists(stagingPath)) {
            std::filesystem::copy_file(stagingPath, finalOutputPath, std::filesystem::copy_options::overwrite_existing, errorCode);
            if (errorCode) {
                appendDebugLog("screen capture helper copy failed error=" + errorCode.message());
                return wideToUtf8(stagingPath.wstring());
            }

            appendDebugLog("screen capture helper staged file ready final=" + finalOutputPath.string());
            return finalOutputPath.string();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    appendDebugLog("screen capture helper staged file missing after wait");
    return std::nullopt;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
