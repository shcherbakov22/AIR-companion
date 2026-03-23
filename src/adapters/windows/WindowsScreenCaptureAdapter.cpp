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
#include <thread>
#include <vector>

namespace companion::adapters::windows {

#ifdef _WIN32
namespace {

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

bool pngEncoderClsid(CLSID& clsid) {
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
        if (wcscmp(encoders[index].MimeType, L"image/png") == 0) {
            clsid = encoders[index].Clsid;
            return true;
        }
    }

    return false;
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
        return captureInteractive(outputDirectory);
    }

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

    const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (screenWidth <= 0 || screenHeight <= 0) {
        return std::nullopt;
    }

    const auto path = outputDirectory + "/screen-capture.png";
    const auto widePath = utf8ToWide(path);

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        return std::nullopt;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc) {
        ReleaseDC(nullptr, screenDc);
        return std::nullopt;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, screenWidth, screenHeight);
    if (!bitmap) {
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return std::nullopt;
    }

    HGDIOBJ previousObject = SelectObject(memoryDc, bitmap);
    const bool copied = BitBlt(memoryDc, 0, 0, screenWidth, screenHeight, screenDc, screenX, screenY, SRCCOPY | CAPTUREBLT) != 0;
    SelectObject(memoryDc, previousObject);

    std::optional<std::string> result;
    if (copied) {
        CLSID encoder{};
        if (pngEncoderClsid(encoder)) {
            Gdiplus::Bitmap image(bitmap, nullptr);
            if (image.Save(widePath.c_str(), &encoder, nullptr) == Gdiplus::Ok) {
                result = path;
            }
        }
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
    std::filesystem::create_directories(outputDirectory);
    const auto stagingDirectory = sharedInteractiveCaptureDirectory();
    std::filesystem::create_directories(stagingDirectory);
    const auto outputPath = stagingDirectory / "screen-capture.png";
    std::error_code errorCode;
    std::filesystem::remove(outputPath, errorCode);

    const auto helperPath = trayBinaryPath();
    if (helperPath.empty() || !std::filesystem::exists(helperPath)) {
        return std::nullopt;
    }

    const auto activeSessionId = WTSGetActiveConsoleSessionId();
    if (activeSessionId == 0xFFFFFFFF) {
        return std::nullopt;
    }

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
    commandLine += L"\" --capture-screen-once \"";
    commandLine += utf8ToWide(outputDirectory);
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

    WaitForSingleObject(processInformation.hProcess, 15000);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);

    for (int attempt = 0; attempt < 30; ++attempt) {
        if (std::filesystem::exists(outputPath)) {
            return wideToUtf8(outputPath.wstring());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return std::nullopt;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
