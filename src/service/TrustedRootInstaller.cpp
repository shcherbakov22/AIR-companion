#include "companion/service/TrustedRootInstaller.h"

#include "companion/networking/HttpClient.h"

#include <optional>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace companion::service {

namespace {

struct ParsedBaseUrl {
    std::string scheme;
    std::string authority;
    std::string host;
    std::optional<unsigned short> port;
};

std::optional<ParsedBaseUrl> parseBaseUrl(const std::string& baseUrl) {
    const auto schemeSeparator = baseUrl.find("://");
    if (schemeSeparator == std::string::npos) {
        return std::nullopt;
    }

    const auto authorityStart = schemeSeparator + 3;
    const auto pathStart = baseUrl.find('/', authorityStart);
    const auto authority = baseUrl.substr(authorityStart, pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
    if (authority.empty()) {
        return std::nullopt;
    }

    ParsedBaseUrl parsed;
    parsed.scheme = baseUrl.substr(0, schemeSeparator);
    parsed.authority = authority;

    if (authority.front() == '[') {
        const auto bracketEnd = authority.find(']');
        if (bracketEnd == std::string::npos) {
            return std::nullopt;
        }

        parsed.host = authority.substr(0, bracketEnd + 1);
        if (bracketEnd + 1 < authority.size() && authority[bracketEnd + 1] == ':') {
            parsed.port = static_cast<unsigned short>(std::stoi(authority.substr(bracketEnd + 2)));
        }

        return parsed;
    }

    const auto portSeparator = authority.rfind(':');
    if (portSeparator != std::string::npos && authority.find(':') == portSeparator) {
        parsed.host = authority.substr(0, portSeparator);
        parsed.port = static_cast<unsigned short>(std::stoi(authority.substr(portSeparator + 1)));
    } else {
        parsed.host = authority;
    }

    return parsed;
}

#ifdef _WIN32

std::optional<std::string> decodeCertificateBytes(const std::string& certificate) {
    if (certificate.empty()) {
        return std::nullopt;
    }

    DWORD encodedSize = 0;
    if (!CryptStringToBinaryA(
            certificate.c_str(),
            static_cast<DWORD>(certificate.size()),
            CRYPT_STRING_BASE64HEADER,
            nullptr,
            &encodedSize,
            nullptr,
            nullptr)) {
        return certificate;
    }

    std::string encoded(static_cast<std::size_t>(encodedSize), '\0');
    if (!CryptStringToBinaryA(
            certificate.c_str(),
            static_cast<DWORD>(certificate.size()),
            CRYPT_STRING_BASE64HEADER,
            reinterpret_cast<BYTE*>(encoded.data()),
            &encodedSize,
            nullptr,
            nullptr)) {
        return std::nullopt;
    }

    encoded.resize(encodedSize);
    return encoded;
}

bool installCertificateIntoStore(const std::string& certificate, DWORD storeLocationFlags) {
    const auto encoded = decodeCertificateBytes(certificate);
    if (!encoded.has_value() || encoded->empty()) {
        return false;
    }

    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_A,
        0,
        0,
        storeLocationFlags | CERT_STORE_OPEN_EXISTING_FLAG,
        "ROOT"
    );
    if (store == nullptr) {
        store = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_A,
            0,
            0,
            storeLocationFlags,
            "ROOT"
        );
    }
    if (store == nullptr) {
        return false;
    }

    const bool added = CertAddEncodedCertificateToStore(
        store,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        reinterpret_cast<const BYTE*>(encoded->data()),
        static_cast<DWORD>(encoded->size()),
        CERT_STORE_ADD_REPLACE_EXISTING,
        nullptr
    ) == TRUE;

    CertCloseStore(store, 0);
    return added;
}

#endif

}  // namespace

bool TrustedRootInstaller::ensureTrustedForBaseUrl(const std::string& baseUrl, const std::string& certificateUrl) const {
    const auto resolvedCertificateUrl = certificateUrl.empty()
        ? rootCertificateUrlForBaseUrl(baseUrl)
        : certificateUrl;
    if (resolvedCertificateUrl.empty()) {
        return false;
    }

    networking::HttpClient httpClient;
    networking::HttpRequestOptions options;
    options.allowInvalidCertificate = resolvedCertificateUrl.rfind("https://", 0) == 0;
    const auto response = httpClient.get(resolvedCertificateUrl, {}, options);
    if (response.statusCode < 200 || response.statusCode >= 300 || response.body.empty()) {
        return false;
    }

#ifdef _WIN32
    if (installCertificateIntoStore(response.body, CERT_SYSTEM_STORE_LOCAL_MACHINE)) {
        return true;
    }

    return installCertificateIntoStore(response.body, CERT_SYSTEM_STORE_CURRENT_USER);
#else
    (void) response;
    return false;
#endif
}

std::string TrustedRootInstaller::rootCertificateUrlForBaseUrl(const std::string& baseUrl) {
    const auto parsed = parseBaseUrl(baseUrl);
    if (!parsed.has_value()) {
        return {};
    }

    std::string url = parsed->scheme + "://";
    url += parsed->host;

    if (parsed->port.has_value()) {
        const auto port = *parsed->port;
        const bool isDefaultHttps = parsed->scheme == "https" && port == 443;
        const bool isDefaultHttp = parsed->scheme == "http" && port == 80;
        if (!isDefaultHttps && !isDefaultHttp) {
            url += ':' + std::to_string(port);
        }
    }

    url += "/companion/root-ca.crt";
    return url;
}

}  // namespace companion::service
