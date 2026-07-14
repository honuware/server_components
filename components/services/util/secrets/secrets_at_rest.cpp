#include "secrets_at_rest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#include <sodium.h>

#include "db_schema/config_secrets.h"
#include "sql_util/table_helpers/config_secrets.h"
#include "util/env.h"
#include "util/logging.h"

namespace Secrets {

namespace {

// --- Local libsodium helpers ---------------------------------------------
// These used to come from Auth::AuthHelper, which pulled business_logic/auth
// into util/secrets (the cycle Phase 1.3 removes). They are thin wrappers over
// libsodium's URL-safe/no-padding base64 (the same variant AuthHelper uses) and
// its CSPRNG, kept private to this translation unit.

Blob RandomBytes(size_t length) {
    Blob buffer(length);
    randombytes_buf(buffer.data(), length);
    return buffer;
}

std::string Base64Encode(const Blob& data) {
    size_t encodedLen = sodium_base64_ENCODED_LEN(
        data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string encoded(encodedLen, '\0');
    sodium_bin2base64(
        &encoded[0],
        encodedLen,
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size(),
        sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    // sodium_bin2base64 NUL-terminates; drop the trailing NUL so the string's
    // size reflects the encoded length.
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

// Throws std::invalid_argument on malformed input (matches the previous
// AuthHelper::Base64Decode contract the callers below rely on).
Blob Base64Decode(std::string_view encoded) {
    Blob decoded(encoded.size());  // base64 expands, so this is always enough
    size_t decodedLen = 0;
    if (sodium_base642bin(
            reinterpret_cast<unsigned char*>(decoded.data()),
            decoded.size(),
            encoded.data(),
            encoded.size(),
            nullptr,
            &decodedLen,
            nullptr,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        throw std::invalid_argument("Base64Decode: invalid base64 input");
    }
    decoded.resize(decodedLen);
    return decoded;
}

// Fixed dev fallback key — 32 bytes of "dev" placeholder. Used only
// when the env var is unset in non-prod mode; logged with a clear
// `[DEV]` warning so it's never confused with a real key. Hardcoded
// (not read from a separate dev secret) so a fresh checkout works
// without any setup.
const Blob& DevFallbackKey() {
    static const Blob kDevKey = []() {
        Blob k(crypto_secretbox_KEYBYTES, std::byte{0});
        // Fill with a recognisable pattern so a hex dump from a dev
        // log makes it obvious this is the dev key, not a real one.
        const char* tag = "honuware-dev-key-not-for-production";
        size_t n = std::min(std::strlen(tag), k.size());
        for (size_t i = 0; i < n; ++i) {
            k[i] = static_cast<std::byte>(tag[i]);
        }
        return k;
    }();
    return kDevKey;
}

}  // namespace

SecretsAtRest::SecretsAtRest(const Blob& key) : key_(key) {
    if (key_.size() != crypto_secretbox_KEYBYTES) {
        throw std::invalid_argument(
            "SecretsAtRest: key must be exactly 32 bytes "
            "(crypto_secretbox_KEYBYTES)");
    }
}

SecretsAtRest::~SecretsAtRest() {
    // Best-effort scrub of the key on destruction.
    if (!key_.empty()) {
        sodium_memzero(key_.data(), key_.size());
    }
}

bool SecretsAtRest::IsEncrypted(std::string_view value) {
    return value.size() >= kVersionPrefix.size()
        && value.compare(0, kVersionPrefix.size(), kVersionPrefix) == 0;
}

std::string SecretsAtRest::Encrypt(std::string_view plaintext) const {
    Blob nonce = RandomBytes(crypto_secretbox_NONCEBYTES);

    Blob ciphertext(plaintext.size() + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(
            reinterpret_cast<unsigned char*>(ciphertext.data()),
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            plaintext.size(),
            reinterpret_cast<const unsigned char*>(nonce.data()),
            reinterpret_cast<const unsigned char*>(key_.data())) != 0) {
        // crypto_secretbox_easy only fails on programmer error
        // (null pointers, etc.) — we shouldn't hit this in practice.
        throw std::runtime_error("SecretsAtRest::Encrypt: secretbox failed");
    }

    // Concatenate nonce + ciphertext into a single blob, then
    // base64-encode for storage in the TEXT column.
    Blob combined;
    combined.reserve(nonce.size() + ciphertext.size());
    combined.insert(combined.end(), nonce.begin(), nonce.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());

    std::string encoded = Base64Encode(combined);
    return std::string(kVersionPrefix) + encoded;
}

std::string SecretsAtRest::Decrypt(std::string_view value) const {
    if (!IsEncrypted(value)) {
        // Legacy plaintext — supports the migration window. After
        // MigrateSecretsToEncrypted runs once at startup, no row
        // takes this path.
        return std::string(value);
    }

    std::string body(value.substr(kVersionPrefix.size()));
    Blob combined;
    try {
        combined = Base64Decode(body);
    } catch (const std::exception&) {
        // Don't echo the inner base64 error — generic failure.
        throw std::runtime_error(
            "SecretsAtRest::Decrypt: invalid encoded value");
    }

    if (combined.size() <
            crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
        throw std::runtime_error(
            "SecretsAtRest::Decrypt: encoded value too short");
    }

    const unsigned char* nonce =
        reinterpret_cast<const unsigned char*>(combined.data());
    const unsigned char* cipher =
        reinterpret_cast<const unsigned char*>(combined.data())
        + crypto_secretbox_NONCEBYTES;
    size_t cipherLen = combined.size() - crypto_secretbox_NONCEBYTES;
    size_t plaintextLen = cipherLen - crypto_secretbox_MACBYTES;

    std::string plaintext(plaintextLen, '\0');
    if (crypto_secretbox_open_easy(
            reinterpret_cast<unsigned char*>(plaintext.data()),
            cipher,
            cipherLen,
            nonce,
            reinterpret_cast<const unsigned char*>(key_.data())) != 0) {
        // MAC failure — either wrong key or tampered ciphertext.
        // The error message intentionally doesn't distinguish them.
        throw std::runtime_error(
            "SecretsAtRest::Decrypt: authentication failed (tampered "
            "or wrong key)");
    }
    return plaintext;
}

SecretsAtRestPtr MakeSecretsAtRest(bool isProd) {
    const char* envVal = Util::GetEnvWithFallback(
        kSecretKeyEnvVar, kSecretKeyEnvVarLegacy);
    bool unset = (envVal == nullptr || envVal[0] == '\0');

    if (unset) {
        if (isProd) {
            throw std::runtime_error(
                std::string("Production mode requires ") + kSecretKeyEnvVar
                + " to be set. ECS task definitions should inject this "
                  "value from AWS Secrets Manager via the `secrets:` block.");
        }
        // Dev / test fallback. Log once-per-process so operators
        // notice if a dev box accidentally went into prod-shaped
        // deployment without the env var.
        LogWarning() << "[secrets_at_rest] event=using_dev_fallback_key "
                     << "reason=env_var_unset env_var=" << kSecretKeyEnvVar
                     << "\n";
        return std::make_shared<SecretsAtRest>(DevFallbackKey());
    }

    // Decode whatever the operator gave us. If the env-var-bearing
    // value isn't valid base64 OR doesn't decode to 32 bytes, fail
    // loudly — silently falling back to dev-key in prod would be a
    // catastrophic security regression.
    Blob key;
    try {
        key = Base64Decode(envVal);
    } catch (const std::exception&) {
        throw std::runtime_error(
            std::string(kSecretKeyEnvVar)
            + " is set but not valid base64. Expected a base64-encoded "
              "32-byte key.");
    }
    if (key.size() != crypto_secretbox_KEYBYTES) {
        throw std::runtime_error(
            std::string(kSecretKeyEnvVar)
            + " decodes to the wrong size. Expected exactly 32 bytes "
              "(crypto_secretbox_KEYBYTES).");
    }
    return std::make_shared<SecretsAtRest>(key);
}

int64_t MigrateSecretsToEncrypted(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    SecretsAtRestPtr secretsAtRest) {
    if (!secretsAtRest) return 0;

    // Read every (name, value) row directly. We can't use
    // SecretsHelper here because that layer would decrypt-then-
    // re-encrypt, which is a no-op on already-encrypted rows but
    // also unnecessary work; the table-helper layer gives us the
    // raw value as stored.
    std::string selectSql = std::string("SELECT ")
        + std::string(DbSchema::kConfigSecretsName) + ", "
        + std::string(DbSchema::kConfigSecretsValue)
        + " FROM " + std::string(DbSchema::kConfigSecretsTable);
    auto rows = transaction.RunSqlStatementReturningKeyValueTableArray(
        selectSql);

    int64_t rewritten = 0;
    TableHelpers::ConfigSecrets configSecrets(databaseHelper);
    for (const auto& row : rows) {
        const std::string nameKey(DbSchema::kConfigSecretsName);
        const std::string valueKey(DbSchema::kConfigSecretsValue);
        if (row.find(nameKey) == row.end() ||
            row.find(valueKey) == row.end()) {
            continue;  // Defensive — shouldn't happen.
        }
        const std::string& name = row.at(nameKey);
        const std::string& value = row.at(valueKey);
        if (SecretsAtRest::IsEncrypted(value)) {
            // Already migrated. Idempotency guarantee comes from
            // here: a second invocation finds nothing to do.
            continue;
        }
        std::string encrypted = secretsAtRest->Encrypt(value);
        configSecrets.AddSecret(transaction, name, encrypted);
        ++rewritten;
    }
    LogInfo() << "[secrets_at_rest] event=migration_completed rewritten="
              << rewritten << "\n";
    return rewritten;
}

}  // namespace Secrets
