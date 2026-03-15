#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objidl.h>
#include <ole2.h>
#include <windows.h>
#include <wrl/client.h>

#include <gdiplus.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {

class ScopedCoInitialize {
public:
    ScopedCoInitialize() : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ScopedCoInitialize() {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool ok() const {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT m_result;
};

class ScopedMfStartup {
public:
    ScopedMfStartup() : m_result(MFStartup(MF_VERSION)) {}

    ~ScopedMfStartup() {
        if (SUCCEEDED(m_result)) {
            MFShutdown();
        }
    }

    [[nodiscard]] bool ok() const {
        return SUCCEEDED(m_result);
    }

private:
    HRESULT m_result;
};

class GdiPlusSession {
public:
    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput input;
        m_status = Gdiplus::GdiplusStartup(&m_token, &input, nullptr);
    }

    ~GdiPlusSession() {
        if (m_status == Gdiplus::Ok) {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    [[nodiscard]] bool ok() const {
        return m_status == Gdiplus::Ok;
    }

private:
    ULONG_PTR m_token{0};
    Gdiplus::Status m_status{Gdiplus::GenericError};
};

std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring converted(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), converted.data(), size);
    return converted;
}

std::optional<CLSID> findEncoderClsid(const wchar_t* mimeType) {
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok || encoderBytes == 0) {
        return std::nullopt;
    }

    std::vector<std::byte> buffer(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return std::nullopt;
    }

    for (UINT index = 0; index < encoderCount; ++index) {
        if (encoders[index].MimeType != nullptr && wcscmp(encoders[index].MimeType, mimeType) == 0) {
            return encoders[index].Clsid;
        }
    }

    return std::nullopt;
}

std::optional<std::string> saveFrameAsPng(
    const std::string& outputDirectory,
    const BYTE* buffer,
    UINT32 width,
    UINT32 height,
    LONG stride) {
    std::filesystem::create_directories(outputDirectory);

    GdiPlusSession gdiPlus;
    if (!gdiPlus.ok()) {
        return std::nullopt;
    }

    const auto encoderClsid = findEncoderClsid(L"image/png");
    if (!encoderClsid.has_value()) {
        return std::nullopt;
    }

    const auto outputPath = std::filesystem::path(outputDirectory) / "camera-capture.png";
    const auto wideOutputPath = outputPath.wstring();

    Gdiplus::Bitmap bitmap(
        static_cast<INT>(width),
        static_cast<INT>(height),
        stride,
        PixelFormat32bppRGB,
        const_cast<BYTE*>(buffer));

    if (bitmap.Save(wideOutputPath.c_str(), &encoderClsid.value(), nullptr) != Gdiplus::Ok) {
        return std::nullopt;
    }

    return outputPath.string();
}

bool readFrameSize(IMFMediaType* mediaType, UINT32& width, UINT32& height) {
    return SUCCEEDED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height));
}

}  // namespace
#endif

namespace companion::adapters::windows {

std::optional<std::string> WindowsCameraCaptureAdapter::captureToFile(const std::string& outputDirectory) {
#ifdef _WIN32
    ScopedCoInitialize coInitialize;
    if (!coInitialize.ok()) {
        return std::nullopt;
    }

    ScopedMfStartup mfStartup;
    if (!mfStartup.ok()) {
        return std::nullopt;
    }

    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) {
        return std::nullopt;
    }

    if (FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        return std::nullopt;
    }

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount)) || deviceCount == 0) {
        if (devices != nullptr) {
            CoTaskMemFree(devices);
        }
        return std::nullopt;
    }

    auto freeDevices = [&]() {
        for (UINT32 index = 0; index < deviceCount; ++index) {
            if (devices[index] != nullptr) {
                devices[index]->Release();
            }
        }
        CoTaskMemFree(devices);
    };

    ComPtr<IMFMediaSource> mediaSource;
    const HRESULT activateResult = devices[0]->ActivateObject(IID_PPV_ARGS(&mediaSource));
    freeDevices();

    if (FAILED(activateResult) || mediaSource == nullptr) {
        return std::nullopt;
    }

    ComPtr<IMFSourceReader> sourceReader;
    if (FAILED(MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &sourceReader))) {
        return std::nullopt;
    }

    ComPtr<IMFMediaType> targetMediaType;
    if (FAILED(MFCreateMediaType(&targetMediaType))) {
        return std::nullopt;
    }

    if (FAILED(targetMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(targetMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(sourceReader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr,
            targetMediaType.Get()))) {
        return std::nullopt;
    }

    ComPtr<IMFMediaType> currentMediaType;
    if (FAILED(sourceReader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentMediaType))) {
        return std::nullopt;
    }

    UINT32 width = 0;
    UINT32 height = 0;
    if (!readFrameSize(currentMediaType.Get(), width, height) || width == 0 || height == 0) {
        return std::nullopt;
    }

    for (int attempt = 0; attempt < 30; ++attempt) {
        DWORD streamIndex = 0;
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;

        const auto readResult = sourceReader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0,
            &streamIndex,
            &streamFlags,
            &timestamp,
            &sample);

        if (FAILED(readResult)) {
            return std::nullopt;
        }

        if ((streamFlags & MF_SOURCE_READERF_STREAMTICK) != 0 || sample == nullptr) {
            Sleep(50);
            continue;
        }

        ComPtr<IMFMediaBuffer> mediaBuffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&mediaBuffer)) || mediaBuffer == nullptr) {
            return std::nullopt;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        if (FAILED(mediaBuffer->Lock(&data, &maxLength, &currentLength)) || data == nullptr || currentLength == 0) {
            return std::nullopt;
        }

        const LONG stride = static_cast<LONG>(width * 4);
        const auto savedPath = saveFrameAsPng(outputDirectory, data, width, height, stride);
        mediaBuffer->Unlock();
        return savedPath;
    }

    return std::nullopt;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
