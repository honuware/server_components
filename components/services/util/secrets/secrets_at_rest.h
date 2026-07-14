#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"  // Transaction, DatabaseHelper
#include "util/types.h"  // Blob

// Phase 1.3 componentization: this at-rest encryption helper lives in
// util/secrets (alongside SecretsHelper, which composes it) rather than
// business_logic/auth. It has no auth-layer dependency — it does its own
// libsodium crypto directly — which breaks the old util -> business_logic/auth
// cycle (secrets_helper.h used to forward-declare Auth::SecretsAtRest).
namespace Secrets {

// Phase 8.1 of the security review: at-rest encryption for the
// `config_secrets.value` column.
//
// Architecture (v1, ECS env var):
//   - `HONUWARE_SECRET_KEY` (legacy fallback: `KNOTTYYOGA_SECRET_KEY`) is a
//     32-byte master key, base64-encoded
//     in the env var. ECS task definitions inject it from AWS
//     Secrets Manager so the container reads a normal env var.
//   - On every secret write, `Encrypt(plaintext)` produces a wire
//     value of the form `v1:` + base64(nonce(24) || ciphertext+MAC).
//     The 24-byte nonce is fresh per-encryption (libsodium's RNG)
//     so the same plaintext encrypts differently each time.
//   - On read, `Decrypt(value)` strips the prefix, base64-decodes,
//     splits nonce + ciphertext, and runs `crypto_secretbox_open_easy`.
//     The MAC catches tampered ciphertext (decrypt throws) and
//     wrong-key situations (decrypt throws — the MAC is keyed).
//
// Migration: a value that doesn't start with `v1:` is treated as
// legacy plaintext and returned as-is from `Decrypt`. The migration
// pass (Phase 8.1c) walks the table once at startup and rewrites
// any unprefixed value with its encrypted form. This means the
// "v1:" prefix is the load-bearing format-version marker — future
// rotations would land as "v2:" and the helper would handle both
// versions during the transition.
//
// Threat model: this protects against "attacker dumps the database"
// — the dumped file alone is useless without the env var. It does
// NOT protect against an attacker who has shell access to a running
// server (they can read both the env var and the data) — that's
// what the v2 KMS architecture is for, deferred until needed.
class SecretsAtRest {
public:
    // Constructs with a 32-byte key. Throws std::invalid_argument
    // if `key.size()` doesn't match libsodium's expected size.
    explicit SecretsAtRest(const Blob& key);

    SecretsAtRest(const SecretsAtRest&) = delete;
    SecretsAtRest& operator=(const SecretsAtRest&) = delete;
    ~SecretsAtRest();

    // Encrypts `plaintext` and returns the wire format
    // `"v1:" + base64(nonce || ciphertext)`. Empty plaintext is
    // valid and produces a non-empty output (the nonce + MAC).
    std::string Encrypt(std::string_view plaintext) const;

    // Decrypts a `v1:`-prefixed value. When the input does NOT
    // start with `v1:`, returns the input as-is (legacy plaintext
    // — supports the migration window).
    //
    // Throws std::runtime_error when:
    //   - The value claims to be `v1:` but the base64 is invalid
    //   - The value claims to be `v1:` but the decoded length is
    //     less than (nonce + MAC)
    //   - The MAC fails (tampered ciphertext OR wrong key)
    // The error messages don't echo any cryptographic detail.
    std::string Decrypt(std::string_view value) const;

    // True when `value` starts with the v1 format prefix. Public
    // for the migration helper (Phase 8.1c) so it can identify
    // legacy plaintext rows.
    static bool IsEncrypted(std::string_view value);

    // The version prefix that identifies a v1-encrypted value.
    // Exposed for tests and for the migration helper.
    static constexpr std::string_view kVersionPrefix = "v1:";

private:
    // Stored as Blob (vector<byte>) so we can `sodium_memzero` it
    // in the destructor.
    Blob key_;
};

using SecretsAtRestPtr = std::shared_ptr<SecretsAtRest>;

// Reads `HONUWARE_SECRET_KEY` (legacy fallback: `KNOTTYYOGA_SECRET_KEY`)
// from the environment, base64-decodes
// it, and returns a SecretsAtRest wrapping the 32-byte key.
//
// In prod mode (`isProd=true`): throws std::runtime_error when the
// env var is unset, empty, or decodes to a wrong-length blob. The
// error message instructs the operator to wire the var via Secrets
// Manager.
//
// In dev/test mode (`isProd=false`): when the env var is unset or
// empty, falls back to a fixed 32-byte dev key (logged once via
// LogWarning) so local builds don't require the var. A wrong-length
// override still throws — operators who explicitly set the var get
// the safety check.
SecretsAtRestPtr MakeSecretsAtRest(bool isProd);

// The env var name. Exposed so main.cpp / tests / docs share one
// definition. Canonical HONUWARE_* name; the legacy KNOTTYYOGA_* name is a
// fallback during the componentization rename (Phase 1.1).
constexpr const char* kSecretKeyEnvVar = "HONUWARE_SECRET_KEY";
constexpr const char* kSecretKeyEnvVarLegacy = "KNOTTYYOGA_SECRET_KEY";

// Phase 8.1c of the security review: walk every row in
// `config_secrets` and re-encrypt any value that's still in legacy
// plaintext form. Idempotent — rows already prefixed with `v1:` are
// left alone, so running this twice (or running it after every
// startup, the recommended pattern) is a no-op the second time.
//
// Returns the number of rows that were actually rewritten.
//
// Migration shape: read every (name, value), check
// `SecretsAtRest::IsEncrypted(value)`. If false, encrypt and write
// through `ConfigSecrets::AddSecret`. Caller is responsible for
// running this inside a single transaction so a partial failure
// rolls back cleanly.
int64_t MigrateSecretsToEncrypted(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    SecretsAtRestPtr secretsAtRest);

}  // namespace Secrets
