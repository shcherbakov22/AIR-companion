#include "companion/networking/HttpClient.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include <optional>
#include <sstream>

namespace companion::networking {

namespace {

#ifdef _WIN32

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

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port{0};
    bool secure{false};
};

std::optional<ParsedUrl> parseUrl(const std::string& url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);

    std::wstring wideUrl = utf8ToWide(url);
    if (wideUrl.empty()) {
        return std::nullopt;
    }

    wchar_t hostBuffer[256]{};
    wchar_t pathBuffer[2048]{};
    components.lpszHostName = hostBuffer;
    components.dwHostNameLength = static_cast<DWORD>(std::size(hostBuffer));
    components.lpszUrlPath = pathBuffer;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(pathBuffer));

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components)) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }

    return parsed;
}

std::wstring buildHeaderBlock(const std::map<std::string, std::string>& headers) {
    std::wostringstream out;
    for (const auto& [name, value] : headers) {
        out << utf8ToWide(name) << L": " << utf8ToWide(value) << L"\r\n";
    }
    return out.str();
}

HttpResponse sendRequest(const std::wstring& method,
                         const std::string& url,
                         const std::map<std::string, std::string>& headers,
                         const std::string& body) {
    const auto parsed = parseUrl(url);
    if (!parsed.has_value()) {
        return {0, {}};
    }

    HttpResponse response;

    const auto session = WinHttpOpen(L"AIRCompanion/0.1",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!session) {
        return response;
    }

    const auto connect = WinHttpConnect(session, parsed->host.c_str(), parsed->port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD requestFlags = parsed->secure ? WINHTTP_FLAG_SECURE : 0;
    const auto request = WinHttpOpenRequest(connect,
                                            method.c_str(),
                                            parsed->path.c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            requestFlags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return response;
    }

    const auto headerBlock = buildHeaderBlock(headers);
    LPVOID bodyData = body.empty() ? WINHTTP_NO_REQUEST_DATA : reinterpret_cast<LPVOID>(body.empty() ? nullptr : const_cast<char*>(body.data()));
    const auto bodySize = static_cast<DWORD>(body.size());

    const bool sent = WinHttpSendRequest(
        request,
        headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
        static_cast<DWORD>(headerBlock.size()),
        bodyData,
        bodySize,
        bodySize,
        0
    );

    if (sent && WinHttpReceiveResponse(request, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode,
                            &statusCodeSize,
                            WINHTTP_NO_HEADER_INDEX);
        response.statusCode = static_cast<int>(statusCode);

        std::string payload;
        DWORD available = 0;
        do {
            available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                break;
            }

            std::string chunk(static_cast<std::size_t>(available), '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &downloaded) || downloaded == 0) {
                break;
            }

            chunk.resize(downloaded);
            payload += chunk;
        } while (available > 0);

        response.body = std::move(payload);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return response;
}

#endif

}  // namespace

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) const {
#ifdef _WIN32
    return sendRequest(L"GET", url, headers, {});
#else
    return HttpResponse{200, "{\"stub\":true,\"url\":\"" + url + "\"}"};
#endif
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body) const {
#ifdef _WIN32
    return sendRequest(L"POST", url, headers, body);
#else
    return HttpResponse{200, "{\"stub\":true,\"url\":\"" + url + "\",\"body\":\"" + body + "\"}"};
#endif
}

}  // namespace companion::networking
