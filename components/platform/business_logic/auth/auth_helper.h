#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <util/types.h>

#include "sql_util/database_access/transaction.h"

namespace Secrets {
class SecretsHelper;
using SecretsHelperPtr = std::shared_ptr<SecretsHelper>;
}  // namespace Secrets

namespace Auth {

class AuthHelper {
public:
    AuthHelper() = default;
    AuthHelper(const AuthHelper&) = default;
    AuthHelper& operator=(const AuthHelper&) = default;
    ~AuthHelper() = default;

    // Hash with a fixed Argon2id cost. Unparameterized form defaults to
    // libsodium's MODERATE (3 ops, 256 MB) to keep production-side callers
    // safe when no secrets context is available; auth callsites should
    // prefer HashPasswordWithSecrets so the cost is configurable at
    // deploy time.
    std::string HashPassword(const std::string& password) const;

    // Hash with explicit Argon2id cost parameters. memLimitBytes is in
    // bytes (libsodium's API), not KB.
    std::string HashPassword(
        const std::string& password,
        std::uint64_t opsLimit,
        std::size_t memLimitBytes) const;

    bool VerifyPassword(const std::string& password, const std::string& hash) const;
    Blob RandomBytes(size_t length) const;
    // These are URL safe
    std::string Base64Encode(const Blob& data) const;
    std::string Base64Encode(const std::string& encoded) const;
    std::vector<std::byte> Base64Decode(const std::string& encoded) const;

    // Deterministic hash of arbitrary binary data (BLAKE2b via libsodium).
    // Returned as lowercase hex string.
    std::string HashBinary(const Blob& data) const;

    // Hash with cost parameters read from the secrets store. Reads
    // kAuthArgon2OpsLimit (number of ops) and kAuthArgon2MemLimitKb
    // (memory limit in kibibytes — libsodium takes bytes, this helper
    // converts). Falls back to MODERATE if either secret is missing,
    // empty, or unparseable. This is the path production auth code
    // (PersonHelper) uses; the test secrets helper overrides these
    // values to INTERACTIVE so tests remain fast.
    static std::string HashPasswordWithSecrets(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        const std::string& password);

    // Constant-time byte-wise equality. Returns false immediately on
    // length mismatch (the length itself isn't a secret in our threat
    // model — we leak the size of one side, never byte-by-byte content).
    // Backed by sodium_memcmp. Use this for any compare where one of the
    // operands is a stored hash / token / verification material.
    static bool ConstantTimeEqual(std::string_view a, std::string_view b);

private:
};

}  // namespace Auth {