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
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#include <vector>

namespace companion::adapters::windows {

#ifdef _WIN32
namespace {
constexpr int kOutputWidth = 1920;
constexpr int kOutputHeight = 1080;
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

std::wstring trayBinaryPath() {
    auto path = currentExecutablePath();
    if (path.empty()) {
        return {};
    }

    const std::wstring needle = L"air_companion_service.exe";
    const auto position = path.rfind(needle);
    if (position != std::wstring::npos) {
        path.replace(position, needle.size(), L"air_companion_tray.exe");
    }
    return path;
}

std::filesystem::path sharedInteractiveCaptureDirectory() {
    if (const auto* publicRoot = std::getenv("PUBLIC"); publicRoot != nullptr && *publicRoot != '\0') {
        return std::filesystem::path(publicRoot) / "AIRCompanion" / "InteractiveCapture";
    }

    return std::filesystem::path("C:\\Users\\Public\\AIRCompanion\\InteractiveCapture");
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
    GdiPlusSession gdiPlusSession;

    const int screenX = 0;
    const int screenY = 0;
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    if (screenWidth <= 0 || screenHeight <= 0) {
        appendDebugLog("screen capture interactive invalid primary screen metrics");
        return std::nullopt;
    }

    const auto path = outputDirectory + "/screen-capture.jpg";

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        appendDebugLog("screen capture interactive GetDC failed");
        return std::nullopt;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc) {
        appendDebugLog("screen capture interactive CreateCompatibleDC failed");
        ReleaseDC(nullptr, screenDc);
        return std::nullopt;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, screenWidth, screenHeight);
    if (!bitmap) {
        appendDebugLog("screen capture interactive CreateCompatibleBitmap failed");
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return std::nullopt;
    }

    HGDIOBJ previousObject = SelectObject(memoryDc, bitmap);
    const bool copied = BitBlt(memoryDc, 0, 0, screenWidth, screenHeight, screenDc, screenX, screenY, SRCCOPY | CAPTUREBLT) != 0;
    SelectObject(memoryDc, previousObject);

    std::optional<std::string> result;
    if (copied) {
        result = saveBitmapAsJpeg(bitmap, screenWidth, screenHeight, path);
        if (result.has_value()) {
            appendDebugLog("screen capture interactive saved path=" + path);
        }
    } else {
        appendDebugLog("screen capture interactive BitBlt failed");
    }

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    return result;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

std::optional<std::string> WindowsScreenCaptureAdapter::captureViaActiveSessionHelper(const std::string& outputDirectory) const {
#ifdef _WIN32
    (void) outputDirectory;
    const auto stagingDirectory = sharedInteractiveCaptureDirectory();
    std::filesystem::create_directories(stagingDirectory);
    const auto stagingPath = stagingDirectory / "screen-capture.jpg";
    std::error_code errorCode;
    std::filesystem::remove(stagingPath, errorCode);
    appendDebugLog("screen capture helper staging=" + wideToUtf8(stagingPath.wstring()));

    const auto helperPath = trayBinaryPath();
    if (helperPath.empty() || !std::filesystem::exists(helperPath)) {
        appendDebugLog("screen capture helper binary missing");
        return std::nullopt;
    }

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

    void* environment = nullptr;
    CreateEnvironmentBlock(&environment, primaryToken, FALSE);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION processInformation{};

    std::wstring commandLine = L"\"";
    commandLine += helperPath;
    commandLine += L"\" --capture-screen-once \"";
    commandLine += stagingDirectory.wstring();
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
        appendDebugLog("screen capture helper CreateProcessAsUserW failed error=" + std::to_string(GetLastError()));
        return std::nullopt;
    }
    appendDebugLog("screen capture helper created process");

    WaitForSingleObject(processInformation.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    appendDebugLog("screen capture helper exitCode=" + std::to_string(exitCode));
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    if (exitCode != 0) {
        return std::nullopt;
    }

    for (int attempt = 0; attempt < 30; ++attempt) {
        if (std::filesystem::exists(stagingPath)) {
            appendDebugLog("screen capture helper staged file ready");
            return wideToUtf8(stagingPath.wstring());
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
