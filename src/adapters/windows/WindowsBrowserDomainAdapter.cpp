#include "companion/adapters/windows/WindowsAdapters.h"

#ifdef _WIN32
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <windows.h>
#endif

namespace {

#ifdef _WIN32
std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string converted(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), converted.data(), size, nullptr, nullptr);
    return converted;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (first >= last) {
        return {};
    }

    return std::string(first, last);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::wstring readWindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const auto copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(static_cast<std::size_t>(copied));
    return title;
}

std::string readProcessName(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return {};
    }

    std::wstring path(4096, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);

    if (!ok || size == 0) {
        return {};
    }

    path.resize(static_cast<std::size_t>(size));
    return toLower(narrow(std::filesystem::path(path).filename().wstring()));
}

bool isBrowserProcess(const std::string& processName) {
    static constexpr std::array<std::string_view, 5> browsers = {
        "chrome.exe",
        "msedge.exe",
        "firefox.exe",
        "brave.exe",
        "opera.exe",
    };

    return std::find(browsers.begin(), browsers.end(), processName) != browsers.end();
}

std::optional<std::string> extractDomainLikeToken(const std::string& title) {
    std::string token;

    auto flush = [&]() -> std::optional<std::string> {
        if (token.empty()) {
            return std::nullopt;
        }

        std::string candidate = trim(token);
        token.clear();

        while (!candidate.empty() && std::ispunct(static_cast<unsigned char>(candidate.back())) != 0 &&
               candidate.back() != '.' && candidate.back() != ':' && candidate.back() != '/') {
            candidate.pop_back();
        }

        while (!candidate.empty() && std::ispunct(static_cast<unsigned char>(candidate.front())) != 0 &&
               candidate.front() != '.' && candidate.front() != ':') {
            candidate.erase(candidate.begin());
        }

        candidate = toLower(candidate);
        if (candidate.starts_with("http://")) {
            candidate.erase(0, 7);
        } else if (candidate.starts_with("https://")) {
            candidate.erase(0, 8);
        }

        const auto slash = candidate.find('/');
        if (slash != std::string::npos) {
            candidate.erase(slash);
        }

        const auto question = candidate.find('?');
        if (question != std::string::npos) {
            candidate.erase(question);
        }

        if (candidate.starts_with("www.")) {
            candidate.erase(0, 4);
        }

        if (candidate.empty() || candidate.find('.') == std::string::npos) {
            return std::nullopt;
        }

        if (candidate.find(' ') != std::string::npos) {
            return std::nullopt;
        }

        return candidate;
    };

    for (const unsigned char character : title) {
        const bool allowed =
            std::isalnum(character) != 0 || character == '.' || character == '-' || character == ':' || character == '/' || character == '?';
        if (allowed) {
            token.push_back(static_cast<char>(character));
            continue;
        }

        if (auto candidate = flush(); candidate.has_value()) {
            return candidate;
        }
    }

    return flush();
}
#endif

}  // namespace

namespace companion::adapters::windows {

std::optional<std::string> WindowsBrowserDomainAdapter::activeDomain() const {
#ifdef _WIN32
    const auto foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return std::nullopt;
    }

    const auto processName = readProcessName(foreground);
    if (!isBrowserProcess(processName)) {
        return std::nullopt;
    }

    const auto title = trim(narrow(readWindowTitle(foreground)));
    if (title.empty()) {
        return std::nullopt;
    }

    return extractDomainLikeToken(title);
#else
    return std::nullopt;
#endif
}

}  // namespace companion::adapters::windows
