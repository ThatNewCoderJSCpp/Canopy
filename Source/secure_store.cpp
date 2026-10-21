#include <canopy/secure_store.hpp>
#include <canopy/build.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace canopy {

namespace {

#if defined(_WIN32)

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::wstring target_name(const std::string& service, const std::string& account) {
    return widen(service + ":" + account);
}

#elif !defined(__APPLE__)

bool have_secret_tool() {
    static const bool found = [] {
        const int status = std::system("command -v secret-tool >/dev/null 2>&1");
        return status == 0;
    }();
    return found;
}

std::string secret_tool_attributes(const std::string& service, const std::string& account) {
    return "service " + quote_argument(service) + " account " + quote_argument(account);
}

#endif

} // namespace

std::string secure_store_name() {
#if defined(_WIN32)
    return "Windows Credential Manager";
#elif defined(__APPLE__)
    return "the macOS Keychain";
#else
    return have_secret_tool() ? "the system keyring" : std::string();
#endif
}

bool secure_store_available() {
    return !secure_store_name().empty();
}

bool secure_store_save(const std::string& service, const std::string& account, const std::string& secret) {
#if defined(_WIN32)
    std::wstring target = target_name(service, account);
    std::wstring user = widen(account);
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target.data();
    cred.UserName = user.data();
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&cred, 0) != FALSE;
#elif defined(__APPLE__)
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(), static_cast<UInt32>(account.size()), account.data(), nullptr, nullptr, &item);
    if (status == errSecSuccess && item) {
        status = SecKeychainItemModifyAttributesAndData(item, nullptr, static_cast<UInt32>(secret.size()), secret.data());
        CFRelease(item);
        return status == errSecSuccess;
    }
    status = SecKeychainAddGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(), static_cast<UInt32>(account.size()), account.data(), static_cast<UInt32>(secret.size()), secret.data(), nullptr);
    return status == errSecSuccess;
#else
    if (!have_secret_tool()) return false;
    const std::string line = "secret-tool store --label=" + quote_argument("Canopy: " + service + " (" + account + ")") + " " + secret_tool_attributes(service, account) + " >/dev/null 2>&1";
    FILE* pipe = popen(line.c_str(), "w");
    if (!pipe) return false;
    std::fwrite(secret.data(), 1, secret.size(), pipe);
    return pclose(pipe) == 0;
#endif
}

std::optional<std::string> secure_store_load(const std::string& service, const std::string& account) {
#if defined(_WIN32)
    const std::wstring target = target_name(service, account);
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred) return std::nullopt;
    std::string secret(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred);
    return secret;
#elif defined(__APPLE__)
    UInt32 length = 0;
    void* data = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(), static_cast<UInt32>(account.size()), account.data(), &length, &data, nullptr);
    if (status != errSecSuccess || !data) return std::nullopt;
    std::string secret(static_cast<const char*>(data), length);
    SecKeychainItemFreeContent(nullptr, data);
    return secret;
#else
    if (!have_secret_tool()) return std::nullopt;
    const std::string line = "secret-tool lookup " + secret_tool_attributes(service, account) + " 2>/dev/null";
    FILE* pipe = popen(line.c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string secret;
    char buffer[512];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) secret.append(buffer, n);
    if (pclose(pipe) != 0 || secret.empty()) return std::nullopt;
    while (!secret.empty() && (secret.back() == '\n' || secret.back() == '\r')) secret.pop_back();
    return secret;
#endif
}

bool secure_store_delete(const std::string& service, const std::string& account) {
#if defined(_WIN32)
    const std::wstring target = target_name(service, account);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
#elif defined(__APPLE__)
    SecKeychainItemRef item = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(), static_cast<UInt32>(account.size()), account.data(), nullptr, nullptr, &item);
    if (status != errSecSuccess || !item) return false;
    const bool ok = SecKeychainItemDelete(item) == errSecSuccess;
    CFRelease(item);
    return ok;
#else
    if (!have_secret_tool()) return false;
    const std::string line = "secret-tool clear " + secret_tool_attributes(service, account) + " >/dev/null 2>&1";
    return std::system(line.c_str()) == 0;
#endif
}

} // namespace canopy
