#include "companion/tray/RemoteControlHelper.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <rfb/rfb.h>
#include <wrl/client.h>
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace companion::tray {

#ifdef _WIN32
namespace {

std::atomic_bool g_running{true};
using Microsoft::WRL::ComPtr;

struct DesktopDuplicator {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> stagingTexture;
    DXGI_OUTPUT_DESC outputDesc{};
    int width{0};
    int height{0};

    bool initialize() {
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL featureLevel{};
        static const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context
        );
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &device,
                &featureLevel,
                &context
            );
            if (FAILED(hr)) {
                return false;
            }
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device.As(&dxgiDevice);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(0, &output);
        if (FAILED(hr)) {
            return false;
        }

        output->GetDesc(&outputDesc);
        width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
        if (width <= 0 || height <= 0) {
            return false;
        }

        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) {
            return false;
        }

        hr = output1->DuplicateOutput(device.Get(), &duplication);
        if (FAILED(hr)) {
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = static_cast<UINT>(width);
        textureDesc.Height = static_cast<UINT>(height);
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = device->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
        return SUCCEEDED(hr);
    }

    bool reinitialize() {
        duplication.Reset();
        stagingTexture.Reset();
        context.Reset();
        device.Reset();
        width = 0;
        height = 0;
        ZeroMemory(&outputDesc, sizeof(outputDesc));
        return initialize();
    }
};

struct RemoteContext {
    rfbScreenInfoPtr server{nullptr};
    std::vector<std::uint8_t> frameBuffer;
    int width{0};
    int height{0};
    int originX{0};
    int originY{0};
    int buttonMask{0};
    bool shiftDown{false};
    bool controlDown{false};
    bool altDown{false};
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

void writeStateFile(const std::string& path, int port) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream output(path, std::ios::trunc);
    output << "{\"pid\":" << GetCurrentProcessId() << ",\"port\":" << port << "}";
}

void removeStateFile(const std::string& path) {
    std::error_code errorCode;
    std::filesystem::remove(path, errorCode);
}

BOOL WINAPI consoleHandler(DWORD signal) {
    switch (signal) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running = false;
            return TRUE;
        default:
            return FALSE;
    }
}

std::optional<WORD> modifierVirtualKey(rfbKeySym keySym) {
    switch (keySym) {
        case 0xFFE1:
        case 0xFFE2:
            return VK_SHIFT;
        case 0xFFE3:
        case 0xFFE4:
            return VK_CONTROL;
        case 0xFFE9:
        case 0xFFEA:
            return VK_MENU;
        default:
            return std::nullopt;
    }
}

void sendVirtualKey(WORD virtualKey, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void updateModifierState(RemoteContext& context, WORD virtualKey, bool down) {
    if (virtualKey == VK_SHIFT) {
        context.shiftDown = down;
    } else if (virtualKey == VK_CONTROL) {
        context.controlDown = down;
    } else if (virtualKey == VK_MENU) {
        context.altDown = down;
    }
}

WORD virtualKeyForKeysym(rfbKeySym keySym, bool& useUnicode, std::wstring& unicodeText, BYTE& requiredModifiers) {
    useUnicode = false;
    unicodeText.clear();
    requiredModifiers = 0;

    if (keySym >= 32 && keySym <= 126) {
        SHORT translated = VkKeyScanW(static_cast<WCHAR>(keySym));
        if (translated != -1) {
            requiredModifiers = HIBYTE(translated);
            return LOBYTE(translated);
        }

        useUnicode = true;
        unicodeText.push_back(static_cast<wchar_t>(keySym));
        return 0;
    }

    switch (keySym) {
        case 0xFF08: return VK_BACK;
        case 0xFF09: return VK_TAB;
        case 0xFF0D: return VK_RETURN;
        case 0xFF1B: return VK_ESCAPE;
        case 0xFFFF: return VK_DELETE;
        case 0xFF50: return VK_HOME;
        case 0xFF51: return VK_LEFT;
        case 0xFF52: return VK_UP;
        case 0xFF53: return VK_RIGHT;
        case 0xFF54: return VK_DOWN;
        case 0xFF55: return VK_PRIOR;
        case 0xFF56: return VK_NEXT;
        case 0xFF57: return VK_END;
        case 0xFF63: return VK_INSERT;
        case 0xFFE1:
        case 0xFFE2: return VK_SHIFT;
        case 0xFFE3:
        case 0xFFE4: return VK_CONTROL;
        case 0xFFE9:
        case 0xFFEA: return VK_MENU;
        case 0xFFBE: return VK_F1;
        case 0xFFBF: return VK_F2;
        case 0xFFC0: return VK_F3;
        case 0xFFC1: return VK_F4;
        case 0xFFC2: return VK_F5;
        case 0xFFC3: return VK_F6;
        case 0xFFC4: return VK_F7;
        case 0xFFC5: return VK_F8;
        case 0xFFC6: return VK_F9;
        case 0xFFC7: return VK_F10;
        case 0xFFC8: return VK_F11;
        case 0xFFC9: return VK_F12;
        default:
            if (keySym > 126 && keySym <= 0xFFFF) {
                useUnicode = true;
                unicodeText.push_back(static_cast<wchar_t>(keySym));
            }
            return 0;
    }
}

void sendUnicodeInput(bool down, const std::wstring& text) {
    for (const wchar_t ch : text) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ch;
        input.ki.dwFlags = KEYEVENTF_UNICODE | (down ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &input, sizeof(INPUT));
    }
}

void keyboardEvent(rfbBool down, rfbKeySym keySym, rfbClientPtr client) {
    auto* context = static_cast<RemoteContext*>(client->screen->screenData);

    if (const auto modifierKey = modifierVirtualKey(keySym)) {
        updateModifierState(*context, *modifierKey, down != 0);
        sendVirtualKey(*modifierKey, down != 0);
        return;
    }

    bool useUnicode = false;
    std::wstring unicodeText;
    BYTE requiredModifiers = 0;
    const WORD virtualKey = virtualKeyForKeysym(keySym, useUnicode, unicodeText, requiredModifiers);
    if (useUnicode) {
        sendUnicodeInput(down != 0, unicodeText);
        return;
    }
    if (virtualKey == 0) {
        return;
    }

    const bool needsShift = (requiredModifiers & 1) != 0;
    const bool needsControl = (requiredModifiers & 2) != 0;
    const bool needsAlt = (requiredModifiers & 4) != 0;
    const bool tempShift = needsShift && !context->shiftDown;
    const bool tempControl = needsControl && !context->controlDown;
    const bool tempAlt = needsAlt && !context->altDown;

    if (down) {
        if (tempShift) {
            sendVirtualKey(VK_SHIFT, true);
        }
        if (tempControl) {
            sendVirtualKey(VK_CONTROL, true);
        }
        if (tempAlt) {
            sendVirtualKey(VK_MENU, true);
        }

        sendVirtualKey(virtualKey, true);
        return;
    }

    sendVirtualKey(virtualKey, false);

    if (tempAlt) {
        sendVirtualKey(VK_MENU, false);
    }
    if (tempControl) {
        sendVirtualKey(VK_CONTROL, false);
    }
    if (tempShift) {
        sendVirtualKey(VK_SHIFT, false);
    }
}

void pointerEvent(int buttonMask, int x, int y, rfbClientPtr client) {
    auto* context = static_cast<RemoteContext*>(client->screen->screenData);
    const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int clampedX = max(0, min(x, context->width - 1));
    const int clampedY = max(0, min(y, context->height - 1));

    INPUT moveInput{};
    moveInput.type = INPUT_MOUSE;
    moveInput.mi.dx = MulDiv(clampedX, 65535, screenWidth - 1);
    moveInput.mi.dy = MulDiv(clampedY, 65535, screenHeight - 1);
    moveInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &moveInput, sizeof(INPUT));

    const struct ButtonInfo {
        int mask;
        DWORD downFlag;
        DWORD upFlag;
    } buttons[] = {
        {1, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP},
        {2, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP},
        {4, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP},
    };

    for (const auto& button : buttons) {
        const bool wasPressed = (context->buttonMask & button.mask) != 0;
        const bool isPressed = (buttonMask & button.mask) != 0;
        if (wasPressed == isPressed) {
            continue;
        }

        INPUT clickInput{};
        clickInput.type = INPUT_MOUSE;
        clickInput.mi.dwFlags = isPressed ? button.downFlag : button.upFlag;
        SendInput(1, &clickInput, sizeof(INPUT));
    }

    context->buttonMask = buttonMask;
}

bool captureFrameWithGdi(RemoteContext& context) {
    context.originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    context.originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    context.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    context.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (context.width <= 0 || context.height <= 0) {
        return false;
    }

    const std::size_t bytes = static_cast<std::size_t>(context.width) * static_cast<std::size_t>(context.height) * 4;
    if (context.frameBuffer.size() != bytes) {
        context.frameBuffer.assign(bytes, 0);
        context.server->frameBuffer = reinterpret_cast<char*>(context.frameBuffer.data());
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, context.width, context.height);
    if (memoryDc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memoryDc != nullptr) {
            DeleteDC(memoryDc);
        }
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ previousObject = SelectObject(memoryDc, bitmap);
    const BOOL copied = BitBlt(
        memoryDc,
        0,
        0,
        context.width,
        context.height,
        screenDc,
        context.originX,
        context.originY,
        SRCCOPY | CAPTUREBLT
    );

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = context.width;
    bitmapInfo.bmiHeader.biHeight = -context.height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    if (copied) {
        GetDIBits(
            memoryDc,
            bitmap,
            0,
            static_cast<UINT>(context.height),
            context.frameBuffer.data(),
            &bitmapInfo,
            DIB_RGB_COLORS
        );
    }

    SelectObject(memoryDc, previousObject);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (!copied) {
        return false;
    }

    rfbMarkRectAsModified(context.server, 0, 0, context.width, context.height);
    return true;
}

bool captureFrameWithDuplication(RemoteContext& context, DesktopDuplicator& duplicator) {
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    HRESULT hr = duplicator.duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return true;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        return duplicator.reinitialize();
    }
    if (FAILED(hr)) {
        return false;
    }

    auto releaseFrame = [&]() {
        duplicator.duplication->ReleaseFrame();
    };

    ComPtr<ID3D11Texture2D> frameTexture;
    hr = desktopResource.As(&frameTexture);
    if (FAILED(hr)) {
        releaseFrame();
        return false;
    }

    D3D11_TEXTURE2D_DESC frameDesc{};
    frameTexture->GetDesc(&frameDesc);
    context.width = static_cast<int>(frameDesc.Width);
    context.height = static_cast<int>(frameDesc.Height);
    context.originX = duplicator.outputDesc.DesktopCoordinates.left;
    context.originY = duplicator.outputDesc.DesktopCoordinates.top;

    const std::size_t bytes = static_cast<std::size_t>(context.width) * static_cast<std::size_t>(context.height) * 4;
    if (context.frameBuffer.size() != bytes) {
        context.frameBuffer.assign(bytes, 0);
        context.server->frameBuffer = reinterpret_cast<char*>(context.frameBuffer.data());
    }

    duplicator.context->CopyResource(duplicator.stagingTexture.Get(), frameTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = duplicator.context->Map(duplicator.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        releaseFrame();
        return false;
    }

    const auto rowBytes = static_cast<std::size_t>(context.width) * 4;
    for (int row = 0; row < context.height; ++row) {
        std::memcpy(
            context.frameBuffer.data() + static_cast<std::size_t>(row) * rowBytes,
            static_cast<std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
            rowBytes
        );
    }

    duplicator.context->Unmap(duplicator.stagingTexture.Get(), 0);
    releaseFrame();

    rfbMarkRectAsModified(context.server, 0, 0, context.width, context.height);
    return true;
}

}  // namespace
#endif

int runRemoteControlHelper(int port, const std::string& stateFilePath) {
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
    RemoteContext context;
    DesktopDuplicator duplicator;
    const bool duplicationReady = duplicator.initialize();
    if (duplicationReady) {
        context.width = duplicator.width;
        context.height = duplicator.height;
        context.originX = duplicator.outputDesc.DesktopCoordinates.left;
        context.originY = duplicator.outputDesc.DesktopCoordinates.top;
    } else {
        context.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        context.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        context.originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        context.originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    }
    if (context.width <= 0 || context.height <= 0) {
        return 1;
    }

    int argc = 0;
    char** argv = nullptr;
    context.server = rfbGetScreen(&argc, argv, context.width, context.height, 8, 3, 4);
    if (context.server == nullptr) {
        return 1;
    }

    context.frameBuffer.assign(static_cast<std::size_t>(context.width) * static_cast<std::size_t>(context.height) * 4, 0);
    context.server->desktopName = const_cast<char*>("AIR Companion");
    context.server->frameBuffer = reinterpret_cast<char*>(context.frameBuffer.data());
    context.server->serverFormat.bitsPerPixel = 32;
    context.server->serverFormat.depth = 24;
    context.server->serverFormat.trueColour = TRUE;
    context.server->serverFormat.redMax = 255;
    context.server->serverFormat.greenMax = 255;
    context.server->serverFormat.blueMax = 255;
    context.server->serverFormat.redShift = 16;
    context.server->serverFormat.greenShift = 8;
    context.server->serverFormat.blueShift = 0;
    context.server->alwaysShared = FALSE;
    context.server->neverShared = TRUE;
    context.server->port = port;
    context.server->kbdAddEvent = keyboardEvent;
    context.server->ptrAddEvent = pointerEvent;
    context.server->screenData = &context;

    if (!(duplicationReady ? captureFrameWithDuplication(context, duplicator) : captureFrameWithGdi(context))) {
        rfbScreenCleanup(context.server);
        return 1;
    }

    rfbInitServer(context.server);
    writeStateFile(stateFilePath, port);

    while (g_running) {
        const bool captured = duplicationReady
            ? captureFrameWithDuplication(context, duplicator)
            : captureFrameWithGdi(context);
        if (!captured) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        rfbProcessEvents(context.server, 10000);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    removeStateFile(stateFilePath);
    rfbScreenCleanup(context.server);
    return 0;
#else
    (void) port;
    (void) stateFilePath;
    return 1;
#endif
}

}  // namespace companion::tray
