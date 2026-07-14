#include "auth_helper.h"

#include <sodium.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"

namespace Auth {

std::string AuthHelper::HashPassword(const std::string& password) const {
    return HashPassword(
        password,
        crypto_pwhash_OPSLIMIT_MODERATE,
        crypto_pwhash_MEMLIMIT_MODERATE);
}

std::string AuthHelper::HashPassword(
    const std::string& password,
    std::uint64_t opsLimit,
    std::size_t memLimitBytes) const {
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
            hash,
            password.c_str(),
            password.size(),
            opsLimit,
            memLimitBytes) != 0) {
        // Phase 2.6: don't leave whatever crypto_pwhash_str managed to
        // partially write sitting on the stack.
        sodium_memzero(hash, sizeof(hash));
        throw std::runtime_error("HashPassword: out of memory");
    }
    std::string out(hash);
    // Phase 2.6: best-effort scrub of the local buffer once it's been
    // copied into the returned string. The string itself still lives in
    // the caller's storage; we don't try to reach into that.
    sodium_memzero(hash, sizeof(hash));
    return out;
}

std::string AuthHelper::HashPasswordWithSecrets(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    const std::string& password) {
    std::uint64_t opsLimit = crypto_pwhash_OPSLIMIT_MODERATE;
    std::size_t memLimitBytes = crypto_pwhash_MEMLIMIT_MODERATE;
    if (secrets) {
        std::string opsStr = secrets->LookupSecret(
            transaction, Secrets::kAuthArgon2OpsLimit);
        if (!opsStr.empty()) {
            try {
                std::uint64_t parsed = std::stoull(opsStr);
                if (parsed >= crypto_pwhash_OPSLIMIT_MIN
                    && parsed <= crypto_pwhash_OPSLIMIT_MAX) {
                    opsLimit = parsed;
                }
            } catch (...) {
                // Fall back to MODERATE; an unparseable secret means a
                // misconfiguration, but we'd rather hash strongly than
                // refuse to authenticate.
            }
        }
        std::string memStr = secrets->LookupSecret(
            transaction, Secrets::kAuthArgon2MemLimitKb);
        if (!memStr.empty()) {
            try {
                std::size_t memKb = static_cast<std::size_t>(
                    std::stoull(memStr));
                std::size_t parsedBytes = memKb * 1024;
                if (parsedBytes >= crypto_pwhash_MEMLIMIT_MIN
                    && parsedBytes <= crypto_pwhash_MEMLIMIT_MAX) {
                    memLimitBytes = parsedBytes;
                }
            } catch (...) {
                // See above.
            }
        }
    }
    AuthHelper helper;
    return helper.HashPassword(password, opsLimit, memLimitBytes);
}

bool AuthHelper::ConstantTimeEqual(std::string_view a, std::string_view b) {
    // sodium_memcmp is constant-time only across equal-length buffers; the
    // length of one operand isn't sensitive in our use cases (stored hash
    // lengths are public schema artifacts), so an early length mismatch
    // returns false without invoking sodium_memcmp.
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool AuthHelper::VerifyPassword(const std::string& password, const std::string& hash) const {
    return crypto_pwhash_str_verify(
        hash.c_str(),
        password.c_str(),
        password.size()) == 0;
}

Blob AuthHelper::RandomBytes(size_t length) const {
    Blob buffer(length);
    randombytes_buf(buffer.data(), length);
    return buffer;
}

std::string AuthHelper::Base64Encode(const Blob& data) const {
    size_t encoded_len = sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string encoded(encoded_len, '\0');
    sodium_bin2base64(
        &encoded[0],
        encoded_len,
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size(),
        sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    // Remove trailing null character if present
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

std::string AuthHelper::Base64Encode(const std::string& encoded) const {
    const std::byte* buffer = reinterpret_cast<const std::byte*>(encoded.c_str());
    return Base64Encode(Blob(buffer, buffer + encoded.size()));
}

std::vector<std::byte> AuthHelper::Base64Decode(const std::string& encoded) const {
    size_t max_decoded_len = encoded.size(); // Base64 expands data, so this is safe
    std::vector<std::byte> decoded(max_decoded_len);
    size_t decoded_len = 0;
    if (sodium_base642bin(
        reinterpret_cast<unsigned char*>(decoded.data()),
        max_decoded_len,
        encoded.c_str(),
        encoded.size(),
        nullptr,
        &decoded_len,
        nullptr,
        sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        throw std::invalid_argument("Base64Decode: invalid base64 input");
    }
    decoded.resize(decoded_len);
    return decoded;
}

std::string AuthHelper::HashBinary(const Blob& data) const {
    unsigned char out[crypto_generichash_BYTES]; // default 32 bytes
    if (crypto_generichash(
            out,
            sizeof(out),
            reinterpret_cast<const unsigned char*>(data.data()),
            data.size(),
            nullptr,
            0) != 0) {
        throw std::runtime_error("HashBinary: crypto_generichash failed");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(out); ++i) {
        oss << std::setw(2) << static_cast<int>(out[i]);
    }
    return oss.str();
}

}  // namespace Auth