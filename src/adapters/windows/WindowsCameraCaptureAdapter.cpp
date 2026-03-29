#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
constexpr DWORD kCameraWarmupMilliseconds = 1500;
constexpr int kDiscardedWarmupFrames = 6;
constexpr int kMaxFrameAttempts = 40;


void appendDebugLog(const std::string& line) {
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
}

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
        appendDebugLog("camera: CoInitialize failed");
        return std::nullopt;
    }

    ScopedMfStartup mfStartup;
    if (!mfStartup.ok()) {
        appendDebugLog("camera: MFStartup failed");
        return std::nullopt;
    }

    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) {
        appendDebugLog("camera: MFCreateAttributes failed");
        return std::nullopt;
    }

    if (FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        appendDebugLog("camera: SetGUID source type failed");
        return std::nullopt;
    }

    IMFActivate** devices = nullptr;
    UINT32 deviceCount = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount)) || deviceCount == 0) {
        appendDebugLog("camera: MFEnumDeviceSources failed or found no devices");
        if (devices != nullptr) {
            CoTaskMemFree(devices);
        }
        return std::nullopt;
    }
    appendDebugLog("camera: enumerated devices=" + std::to_string(deviceCount));

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
        appendDebugLog("camera: ActivateObject failed");
        return std::nullopt;
    }

    ComPtr<IMFAttributes> sourceReaderAttributes;
    if (FAILED(MFCreateAttributes(&sourceReaderAttributes, 2))) {
        appendDebugLog("camera: MFCreateAttributes for source reader failed");
        return std::nullopt;
    }

    if (FAILED(sourceReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))) {
        appendDebugLog("camera: enabling video processing failed");
        return std::nullopt;
    }

    ComPtr<IMFSourceReader> sourceReader;
    if (FAILED(MFCreateSourceReaderFromMediaSource(mediaSource.Get(), sourceReaderAttributes.Get(), &sourceReader))) {
        appendDebugLog("camera: MFCreateSourceReaderFromMediaSource failed");
        return std::nullopt;
    }

    ComPtr<IMFMediaType> targetMediaType;
    if (FAILED(MFCreateMediaType(&targetMediaType))) {
        appendDebugLog("camera: MFCreateMediaType failed");
        return std::nullopt;
    }

    if (FAILED(targetMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(targetMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(sourceReader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            nullptr,
            targetMediaType.Get()))) {
        appendDebugLog("camera: SetCurrentMediaType RGB32 failed");
        return std::nullopt;
    }

    ComPtr<IMFMediaType> currentMediaType;
    if (FAILED(sourceReader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentMediaType))) {
        appendDebugLog("camera: GetCurrentMediaType failed");
        return std::nullopt;
    }

    UINT32 width = 0;
    UINT32 height = 0;
    if (!readFrameSize(currentMediaType.Get(), width, height) || width == 0 || height == 0) {
        appendDebugLog("camera: invalid frame size");
        return std::nullopt;
    }
    appendDebugLog("camera: frame size=" + std::to_string(width) + "x" + std::to_string(height));

    appendDebugLog("camera: warming up for " + std::to_string(kCameraWarmupMilliseconds) + "ms");
    Sleep(kCameraWarmupMilliseconds);

    int capturedFrames = 0;
    for (int attempt = 0; attempt < kMaxFrameAttempts; ++attempt) {
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
            appendDebugLog("camera: ReadSample failed");
            return std::nullopt;
        }

        if ((streamFlags & MF_SOURCE_READERF_STREAMTICK) != 0 || sample == nullptr) {
            appendDebugLog("camera: stream tick or null sample attempt=" + std::to_string(attempt));
            Sleep(50);
            continue;
        }

        ++capturedFrames;
        if (capturedFrames <= kDiscardedWarmupFrames) {
            appendDebugLog("camera: discarding warmup frame=" + std::to_string(capturedFrames));
            Sleep(50);
            continue;
        }

        ComPtr<IMFMediaBuffer> mediaBuffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&mediaBuffer)) || mediaBuffer == nullptr) {
            appendDebugLog("camera: ConvertToContiguousBuffer failed");
            return std::nullopt;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        if (FAILED(mediaBuffer->Lock(&data, &maxLength, &currentLength)) || data == nullptr || currentLength == 0) {
            appendDebugLog("camera: mediaBuffer lock failed");
            return std::nullopt;
        }

        const LONG stride = static_cast<LONG>(width * 4);
        const auto savedPath = saveFrameAsPng(outputDirectory, data, width, height, stride);
        mediaBuffer->Unlock();
        appendDebugLog("camera: save result=" + std::string(savedPath.has_value() ? *savedPath : "null"));
        return savedPath;
    }

    appendDebugLog("camera: no sample received after retries");
    return std::nullopt;
#else
    (void) outputDirectory;
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
