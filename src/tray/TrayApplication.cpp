#include "companion/tray/TrayApplication.h"

#include "companion/service/CompanionConfigStore.h"
#include "companion/service/EnrollmentRequestStore.h"
#include "companion/adapters/windows/WindowsAdapters.h"
#include "companion/tray/EnrollmentWindow.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace companion::tray {

namespace {

#ifdef _WIN32

constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kStatusTimerId = 1;
constexpr int kMenuStatus = 1001;
constexpr int kMenuSettings = 1002;
constexpr int kMenuInstallService = 1003;
constexpr int kMenuStartService = 1004;
constexpr int kMenuStopService = 1005;
constexpr int kMenuLogs = 1006;
constexpr int kMenuExit = 1007;
constexpr wchar_t kTrayWindowClass[] = L"AIRCompanionTrayWindow";

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

std::string initialStatusText() {
    return "AIR Companion running";
}

std::string debugLogPath() {
    if (const auto* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::string(appData) + "\\AIRCompanion\\debug.log";
    }

    return ".\\AIRCompanion\\debug.log";
}

service::EnrollmentRequest initialEnrollmentRequest() {
    service::EnrollmentRequestStore enrollmentRequestStore;
    service::CompanionConfigStore configStore;

    auto draft = enrollmentRequestStore.loadDraft().value_or(service::EnrollmentRequest{
        "https://192.168.11.228",
        {},
        {},
        {},
        {},
    });

    if (const auto stored = configStore.load(); stored.has_value()) {
        if (draft.baseUrl.empty()) {
            draft.baseUrl = stored->baseUrl;
        }
        if (draft.username.empty()) {
            draft.username = stored->identity.studentUsername;
        }
        if (draft.deviceLabel.empty()) {
            draft.deviceLabel = stored->identity.deviceLabel;
        }
        if (draft.rootCaUrl.empty()) {
            draft.rootCaUrl = stored->rootCaUrl;
        }
    }

    return draft;
}

LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* application = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_NCCREATE: {
            const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }

        case WM_COMMAND:
            if (application == nullptr) {
                return 0;
            }

            switch (LOWORD(wParam)) {
                case kMenuStatus: {
                    const auto status = application->currentStatus();
                    const auto messageText = status + "\n\nDebug log:\n" + debugLogPath();
                    MessageBoxW(window, utf8ToWide(messageText).c_str(), L"AIR Companion Status", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                case kMenuSettings: {
                    service::EnrollmentRequestStore enrollmentRequestStore;
                    service::CompanionConfigStore configStore;

                    const auto request = EnrollmentWindow::prompt(
                        initialEnrollmentRequest(),
                        "Save new AIR enrollment settings. Restart the tray after saving."
                    );

                    if (request.has_value() && enrollmentRequestStore.save(*request)) {
                        (void) configStore.clear();
                        MessageBoxW(
                            window,
                            L"Enrollment settings saved. Restart AIR Companion to apply them.",
                            L"AIR Companion",
                            MB_OK | MB_ICONINFORMATION
                        );
                    }
                    return 0;
                }

                case kMenuLogs:
                    ShellExecuteW(
                        nullptr,
                        L"open",
                        L"notepad.exe",
                        utf8ToWide(debugLogPath()).c_str(),
                        nullptr,
                        SW_SHOWNORMAL
                    );
                    return 0;

                case kMenuInstallService:
                case kMenuStartService:
                case kMenuStopService: {
                    companion::adapters::windows::WindowsServiceLifecycleAdapter serviceLifecycleAdapter;

                    bool ok = false;
                    const wchar_t* actionText = L"service action";
                    if (LOWORD(wParam) == kMenuInstallService) {
                        ok = serviceLifecycleAdapter.install();
                        actionText = L"install service";
                    } else if (LOWORD(wParam) == kMenuStartService) {
                        ok = serviceLifecycleAdapter.start();
                        actionText = L"start service";
                    } else {
                        ok = serviceLifecycleAdapter.stop();
                        actionText = L"stop service";
                    }

                    MessageBoxW(
                        window,
                        ok ? L"Service action completed." : L"Service action failed.",
                        actionText,
                        MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR)
                    );
                    return 0;
                }

                case kMenuExit:
                    DestroyWindow(window);
                    return 0;
            }
            return 0;

        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU || lParam == WM_LBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuStatus, L"Status");
                AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
                AppendMenuW(menu, MF_STRING, kMenuInstallService, L"Install service");
                AppendMenuW(menu, MF_STRING, kMenuStartService, L"Start service");
                AppendMenuW(menu, MF_STRING, kMenuStopService, L"Stop service");
                AppendMenuW(menu, MF_STRING, kMenuLogs, L"Open debug log");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

                POINT cursor{};
                GetCursorPos(&cursor);
                SetForegroundWindow(window);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
                DestroyMenu(menu);
                return 0;
            }
            return 0;

        case WM_TIMER:
            if (application != nullptr && wParam == kStatusTimerId) {
                NOTIFYICONDATAW notifyData{};
                notifyData.cbSize = sizeof(notifyData);
                notifyData.hWnd = window;
                notifyData.uID = kTrayIconId;
                notifyData.uFlags = NIF_TIP;
                const auto status = utf8ToWide(application->currentStatus());
                wcsncpy_s(notifyData.szTip, _countof(notifyData.szTip), status.c_str(), _TRUNCATE);
                Shell_NotifyIconW(NIM_MODIFY, &notifyData);
            }
            return 0;

        case WM_DESTROY: {
            NOTIFYICONDATAW notifyData{};
            notifyData.cbSize = sizeof(notifyData);
            notifyData.hWnd = window;
            notifyData.uID = kTrayIconId;
            Shell_NotifyIconW(NIM_DELETE, &notifyData);
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ensureTrayWindowClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = trayWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kTrayWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));

    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    registered = true;
    return true;
}

#endif

}  // namespace

TrayApplication::TrayApplication(core::Agent& agent) : m_agent(agent), m_lastStatus(initialStatusText()) {}

int TrayApplication::run() {
    m_agent.start();

#ifdef _WIN32
    if (!ensureTrayWindowClass()) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kTrayWindowClass,
        L"AIR Companion",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        this
    );

    if (window == nullptr) {
        return 1;
    }

    NOTIFYICONDATAW notifyData{};
    notifyData.cbSize = sizeof(notifyData);
    notifyData.hWnd = window;
    notifyData.uID = kTrayIconId;
    notifyData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notifyData.uCallbackMessage = kTrayMessage;
    notifyData.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    {
        const auto status = utf8ToWide(currentStatus());
        wcsncpy_s(notifyData.szTip, _countof(notifyData.szTip), status.c_str(), _TRUNCATE);
    }
    Shell_NotifyIconW(NIM_ADD, &notifyData);

    SetTimer(window, kStatusTimerId, 2000, nullptr);

    m_workerThread = std::thread([this] {
        runAgentLoop();
    });

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    m_exitRequested = true;
    m_agent.stop();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    return 0;
#else
    while (m_agent.running()) {
        m_agent.tick();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
#endif
}

std::string TrayApplication::currentStatus() const {
    std::scoped_lock lock(m_statusMutex);
    return m_lastStatus;
}

void TrayApplication::runAgentLoop() {
    while (!m_exitRequested && m_agent.running()) {
        m_agent.tick();
        {
            std::scoped_lock lock(m_statusMutex);
            m_lastStatus = m_agent.statusSummary();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

}  // namespace companion::tray
