#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

#include <filesystem>
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

}  // namespace
#endif

std::optional<std::string> WindowsScreenCaptureAdapter::captureToFile(const std::string& outputDirectory) {
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
    (void)outputDirectory;
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
