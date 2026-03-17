#include "companion/tray/EnrollmentWindow.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <optional>
#include <string>

namespace companion::tray {

namespace {

constexpr wchar_t kWindowClassName[] = L"AIRCompanionEnrollmentWindow";
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 290;

enum ControlId : int {
    IdBaseUrl = 1001,
    IdUsername = 1002,
    IdPassword = 1003,
    IdDeviceLabel = 1004,
    IdSave = 1101,
    IdCancel = 1102,
    IdStatus = 1201,
};

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    return wide;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::wstring readWindowText(HWND window) {
    const auto length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void centerWindow(HWND window) {
    RECT windowRect{};
    GetWindowRect(window, &windowRect);

    const auto width = windowRect.right - windowRect.left;
    const auto height = windowRect.bottom - windowRect.top;

    const auto screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const auto screenHeight = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(
        window,
        nullptr,
        (screenWidth - width) / 2,
        (screenHeight - height) / 2,
        0,
        0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    );
}

struct WindowState {
    service::EnrollmentRequest initial;
    std::string statusMessage;
    std::optional<service::EnrollmentRequest> result;
    HWND baseUrlEdit{nullptr};
    HWND usernameEdit{nullptr};
    HWND passwordEdit{nullptr};
    HWND deviceLabelEdit{nullptr};
    HFONT font{nullptr};
};

HWND createLabel(HWND parent, HFONT font, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(
        0,
        L"STATIC",
        text,
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

HWND createEdit(HWND parent,
                HFONT font,
                int controlId,
                const std::wstring& value,
                int x,
                int y,
                int width,
                int height,
                DWORD extraStyle = 0) {
    HWND control = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        value.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        GetModuleHandleW(nullptr),
        nullptr
    );
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

HWND createButton(HWND parent, HFONT font, int controlId, const wchar_t* text, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        GetModuleHandleW(nullptr),
        nullptr
    );
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

service::EnrollmentRequest collectRequest(const WindowState& state) {
    service::EnrollmentRequest request;
    request.baseUrl = wideToUtf8(readWindowText(state.baseUrlEdit));
    request.username = wideToUtf8(readWindowText(state.usernameEdit));
    request.password = wideToUtf8(readWindowText(state.passwordEdit));
    request.deviceLabel = wideToUtf8(readWindowText(state.deviceLabelEdit));
    return request;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* createState = reinterpret_cast<WindowState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createState));
            return TRUE;
        }

        case WM_CREATE: {
            if (state == nullptr) {
                return -1;
            }

            state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            createLabel(window, state->font, L"AIR server URL", 20, 18, 120, 20);
            state->baseUrlEdit = createEdit(window, state->font, IdBaseUrl, utf8ToWide(state->initial.baseUrl), 150, 16, 280, 24);

            createLabel(window, state->font, L"Username", 20, 56, 120, 20);
            state->usernameEdit = createEdit(window, state->font, IdUsername, utf8ToWide(state->initial.username), 150, 54, 280, 24);

            createLabel(window, state->font, L"Password", 20, 94, 120, 20);
            state->passwordEdit = createEdit(window, state->font, IdPassword, utf8ToWide(state->initial.password), 150, 92, 280, 24, ES_PASSWORD);

            createLabel(window, state->font, L"Device label", 20, 132, 120, 20);
            state->deviceLabelEdit = createEdit(window, state->font, IdDeviceLabel, utf8ToWide(state->initial.deviceLabel), 150, 130, 280, 24);

            const auto statusText = state->statusMessage.empty()
                ? L"Enter AIR enrollment details for this device."
                : utf8ToWide(state->statusMessage);
            createLabel(window, state->font, statusText.c_str(), 20, 172, 410, 34);

            createButton(window, state->font, IdSave, L"Enroll", 244, 220, 90, 28);
            createButton(window, state->font, IdCancel, L"Cancel", 340, 220, 90, 28);

            centerWindow(window);
            return 0;
        }

        case WM_COMMAND: {
            if (state == nullptr) {
                return 0;
            }

            const auto controlId = LOWORD(wParam);
            if (controlId == IdSave) {
                auto request = collectRequest(*state);
                if (request.baseUrl.empty() || request.username.empty() || request.password.empty() || request.deviceLabel.empty()) {
                    MessageBoxW(window, L"All fields are required.", L"AIR Companion", MB_OK | MB_ICONWARNING);
                    return 0;
                }

                state->result = std::move(request);
                DestroyWindow(window);
                return 0;
            }

            if (controlId == IdCancel) {
                DestroyWindow(window);
                return 0;
            }

            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureWindowClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    registered = true;
    return true;
}

}  // namespace

std::optional<service::EnrollmentRequest> EnrollmentWindow::prompt(
    const service::EnrollmentRequest& initial,
    const std::string& statusMessage
) {
    if (!ensureWindowClass()) {
        return std::nullopt;
    }

    WindowState state{initial, statusMessage};

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClassName,
        L"AIR Companion Enrollment",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state
    );

    if (window == nullptr) {
        return std::nullopt;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return state.result;
}

}  // namespace companion::tray

#else

namespace companion::tray {

std::optional<service::EnrollmentRequest> EnrollmentWindow::prompt(
    const service::EnrollmentRequest& initial,
    const std::string& statusMessage
) {
    (void) initial;
    (void) statusMessage;
    return std::nullopt;
}

}  // namespace companion::tray

#endif
