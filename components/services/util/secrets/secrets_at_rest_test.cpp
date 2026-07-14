#include "secrets_at_rest.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <sodium.h>

#include "sql_util/table_helpers/config_secrets.h"
#include "test/src/util/database_test_helper.h"

// Phase 8.1 of the security review.
//
// Tests cover the at-rest encryption helper in isolation:
//   - Round-trip preserves the plaintext, including empty strings
//     and embedded nulls
//   - Tampered ciphertext is rejected (MAC catches it)
//   - Decrypting with a different key is rejected (MAC depends on key)
//   - Each Encrypt call uses a fresh nonce — same plaintext encrypts
//     differently each time
//   - Legacy plaintext (no `v1:` prefix) passes through Decrypt as-is
//     — supports the migration window
//   - Wrong-length key rejected at construction time
//
// MakeSecretsAtRest tests cover the env-var resolution: prod-fail,
// dev-fallback, base64-validation, length-validation. These manage
// process-wide env state via a small RAII guard.
//
// Phase 1.3: this test moved from business_logic/auth alongside the helper
// and no longer depends on Auth::AuthHelper — it base64-encodes test keys via
// a local libsodium helper, keeping the util/secrets test util-level.

namespace Secrets {
namespace {

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// RAII guard: scrub on entry and exit so env state can't leak between
// tests. Phase 1.1 rename — scrub BOTH the canonical HONUWARE name and the
// legacy KNOTTYYOGA fallback, otherwise a legacy var set in the developer's
// launch environment could be picked up via the fallback and perturb the
// "unset" tests.
class SecretKeyEnvScope {
public:
    SecretKeyEnvScope() { ScrubAll(); }
    ~SecretKeyEnvScope() { ScrubAll(); }
private:
    static void ScrubAll() {
        UnsetEnv(kSecretKeyEnvVar);
        UnsetEnv(kSecretKeyEnvVarLegacy);
    }
};

// Local URL-safe/no-padding base64 encode (libsodium) — replaces the old
// AuthHelper::Base64Encode the test used to lean on. Only encoding is needed
// here (to build env-var key material); decoding is exercised through the
// helper's public API.
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
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

// Helper: produce a 32-byte test key. Different tests get different
// keys when needed.
Blob TestKey(unsigned char fillByte = 0xAB) {
    return Blob(crypto_secretbox_KEYBYTES, std::byte{fillByte});
}

// ---- Construction ----

TEST(SecretsAtRestTest, RejectsKeyWrongSize) {
    EXPECT_THROW(SecretsAtRest{Blob{}}, std::invalid_argument);
    EXPECT_THROW(SecretsAtRest{Blob(16, std::byte{0})}, std::invalid_argument);
    EXPECT_THROW(SecretsAtRest{Blob(31, std::byte{0})}, std::invalid_argument);
    EXPECT_THROW(SecretsAtRest{Blob(33, std::byte{0})}, std::invalid_argument);
    EXPECT_THROW(SecretsAtRest{Blob(64, std::byte{0})}, std::invalid_argument);
}

TEST(SecretsAtRestTest, AcceptsKeyOf32Bytes) {
    EXPECT_NO_THROW(SecretsAtRest{TestKey()});
}

// ---- Round-trip ----

TEST(SecretsAtRestTest, EncryptDecryptRoundTrip) {
    SecretsAtRest helper(TestKey());
    const std::string plain = "hunter2-the-actual-password";
    std::string encrypted = helper.Encrypt(plain);
    EXPECT_TRUE(SecretsAtRest::IsEncrypted(encrypted));
    EXPECT_NE(encrypted.find("v1:"), std::string::npos);
    EXPECT_EQ(helper.Decrypt(encrypted), plain);
}

TEST(SecretsAtRestTest, RoundTripPreservesEmptyString) {
    SecretsAtRest helper(TestKey());
    std::string encrypted = helper.Encrypt("");
    // Even empty plaintext produces a non-empty wire value (the
    // nonce + MAC bytes).
    EXPECT_FALSE(encrypted.empty());
    EXPECT_TRUE(SecretsAtRest::IsEncrypted(encrypted));
    EXPECT_EQ(helper.Decrypt(encrypted), "");
}

TEST(SecretsAtRestTest, RoundTripPreservesEmbeddedNulls) {
    // libsodium's crypto_secretbox is byte-exact regardless of
    // content. Make sure we don't truncate at a NUL during the
    // string round-trip.
    SecretsAtRest helper(TestKey());
    std::string plain;
    plain.push_back('a');
    plain.push_back('\0');
    plain.push_back('b');
    plain.push_back('\0');
    plain.push_back('c');
    std::string encrypted = helper.Encrypt(plain);
    std::string decrypted = helper.Decrypt(encrypted);
    EXPECT_EQ(decrypted.size(), plain.size());
    EXPECT_EQ(decrypted, plain);
}

TEST(SecretsAtRestTest, EachEncryptUsesAFreshNonce) {
    // Same plaintext, two Encrypt calls — outputs must differ.
    // Without per-call randomness, the same secret would always
    // produce the same ciphertext, leaking equality across rows.
    SecretsAtRest helper(TestKey());
    const std::string plain = "the-same-plaintext";
    std::string a = helper.Encrypt(plain);
    std::string b = helper.Encrypt(plain);
    EXPECT_NE(a, b);
    // Both should still decrypt to the same plaintext.
    EXPECT_EQ(helper.Decrypt(a), plain);
    EXPECT_EQ(helper.Decrypt(b), plain);
}

// ---- Tamper / wrong-key detection ----

TEST(SecretsAtRestTest, DecryptOfTamperedCiphertextThrows) {
    SecretsAtRest helper(TestKey());
    std::string encrypted = helper.Encrypt("the-secret");
    // Flip a bit in the encoded body (after the "v1:" prefix). The
    // MAC has to catch this regardless of which byte we touch.
    ASSERT_GT(encrypted.size(), 5u);
    encrypted[encrypted.size() - 1] =
        (encrypted[encrypted.size() - 1] == 'A' ? 'B' : 'A');
    EXPECT_THROW(helper.Decrypt(encrypted), std::runtime_error);
}

TEST(SecretsAtRestTest, DecryptWithWrongKeyThrows) {
    SecretsAtRest encryptHelper(TestKey(0xAB));
    SecretsAtRest decryptHelper(TestKey(0xCD));
    std::string encrypted = encryptHelper.Encrypt("the-secret");
    EXPECT_THROW(decryptHelper.Decrypt(encrypted), std::runtime_error);
}

TEST(SecretsAtRestTest, DecryptOfTooShortCiphertextThrows) {
    SecretsAtRest helper(TestKey());
    // "v1:" + base64 of a 4-byte string — way under nonce + MAC.
    Blob tiny(4, std::byte{1});
    std::string encoded = Base64Encode(tiny);
    std::string fake = "v1:" + encoded;
    EXPECT_THROW(helper.Decrypt(fake), std::runtime_error);
}

TEST(SecretsAtRestTest, DecryptOfInvalidBase64Throws) {
    SecretsAtRest helper(TestKey());
    EXPECT_THROW(helper.Decrypt("v1:not-valid-base64!@#$%"),
                 std::runtime_error);
}

// ---- Legacy passthrough (migration window) ----

TEST(SecretsAtRestTest, DecryptOfPlaintextPassesThrough) {
    // Pre-migration rows look like raw plaintext (no `v1:` prefix).
    // The helper must return them as-is so the read path still
    // works during the migration window.
    SecretsAtRest helper(TestKey());
    EXPECT_EQ(helper.Decrypt("legacy-plain-secret"), "legacy-plain-secret");
    EXPECT_EQ(helper.Decrypt(""), "");
    EXPECT_EQ(helper.Decrypt("anything-without-the-prefix"),
              "anything-without-the-prefix");
}

TEST(SecretsAtRestTest, IsEncryptedRecognizesPrefix) {
    EXPECT_TRUE(SecretsAtRest::IsEncrypted("v1:base64stuff"));
    EXPECT_FALSE(SecretsAtRest::IsEncrypted(""));
    EXPECT_FALSE(SecretsAtRest::IsEncrypted("v"));
    EXPECT_FALSE(SecretsAtRest::IsEncrypted("v1"));
    EXPECT_FALSE(SecretsAtRest::IsEncrypted("v2:future-format"));
    EXPECT_FALSE(SecretsAtRest::IsEncrypted("legacy-plain"));
    // Substring at non-zero position doesn't count.
    EXPECT_FALSE(SecretsAtRest::IsEncrypted("xv1:foo"));
}

// ---- MakeSecretsAtRest: env-var resolution ----

TEST(MakeSecretsAtRestTest, ProdModeFailsWhenEnvVarUnset) {
    SecretKeyEnvScope envScope;
    EXPECT_THROW(MakeSecretsAtRest(/*isProd=*/true), std::runtime_error);
}

TEST(MakeSecretsAtRestTest, ProdModeFailsWhenEnvVarEmpty) {
    SecretKeyEnvScope envScope;
    SetEnv(kSecretKeyEnvVar, "");
    EXPECT_THROW(MakeSecretsAtRest(/*isProd=*/true), std::runtime_error);
}

TEST(MakeSecretsAtRestTest, ProdModeFailsWhenEnvVarNotBase64) {
    SecretKeyEnvScope envScope;
    SetEnv(kSecretKeyEnvVar, "not-valid-base64!@#$%");
    EXPECT_THROW(MakeSecretsAtRest(/*isProd=*/true), std::runtime_error);
}

TEST(MakeSecretsAtRestTest, ProdModeFailsWhenEnvVarWrongLength) {
    SecretKeyEnvScope envScope;
    // 16 bytes base64-encoded — valid base64 but wrong length.
    Blob shortKey(16, std::byte{0});
    std::string encoded = Base64Encode(shortKey);
    SetEnv(kSecretKeyEnvVar, encoded.c_str());
    EXPECT_THROW(MakeSecretsAtRest(/*isProd=*/true), std::runtime_error);
}

TEST(MakeSecretsAtRestTest, ProdModeAcceptsValidEnvVar) {
    SecretKeyEnvScope envScope;
    Blob key(crypto_secretbox_KEYBYTES, std::byte{0x42});
    std::string encoded = Base64Encode(key);
    SetEnv(kSecretKeyEnvVar, encoded.c_str());

    auto helper = MakeSecretsAtRest(/*isProd=*/true);
    ASSERT_NE(helper, nullptr);
    // Round-trip through the helper to confirm the key was applied.
    EXPECT_EQ(helper->Decrypt(helper->Encrypt("hello")), "hello");
}

TEST(MakeSecretsAtRestTest, ProdModeAcceptsLegacyEnvVarName) {
    // Phase 1.1 rename: the legacy KNOTTYYOGA_SECRET_KEY still works when
    // the canonical HONUWARE_SECRET_KEY is unset, so an existing deploy
    // keeps booting through the transition.
    SecretKeyEnvScope envScope;
    Blob key(crypto_secretbox_KEYBYTES, std::byte{0x42});
    std::string encoded = Base64Encode(key);
    SetEnv(kSecretKeyEnvVarLegacy, encoded.c_str());

    auto helper = MakeSecretsAtRest(/*isProd=*/true);
    ASSERT_NE(helper, nullptr);
    EXPECT_EQ(helper->Decrypt(helper->Encrypt("hello")), "hello");
}

TEST(MakeSecretsAtRestTest, CanonicalNameTakesPrecedenceOverLegacy) {
    // When both names are set, the canonical HONUWARE key is the one
    // applied — a value encrypted with it must NOT decrypt under a helper
    // built from the legacy-only key.
    SecretKeyEnvScope envScope;
    Blob canonicalKey(crypto_secretbox_KEYBYTES, std::byte{0x11});
    Blob legacyKey(crypto_secretbox_KEYBYTES, std::byte{0x22});
    SetEnv(kSecretKeyEnvVar, Base64Encode(canonicalKey).c_str());
    SetEnv(kSecretKeyEnvVarLegacy, Base64Encode(legacyKey).c_str());

    auto both = MakeSecretsAtRest(/*isProd=*/true);
    ASSERT_NE(both, nullptr);
    std::string ct = both->Encrypt("precedence-check");

    // Now build a helper directly from the legacy key and confirm it can
    // NOT read the ciphertext — proving the canonical key was the one used.
    SecretsAtRest legacyOnly(legacyKey);
    EXPECT_THROW(legacyOnly.Decrypt(ct), std::runtime_error);

    // And a helper from the canonical key CAN read it.
    SecretsAtRest canonicalOnly(canonicalKey);
    EXPECT_EQ(canonicalOnly.Decrypt(ct), "precedence-check");
}

TEST(MakeSecretsAtRestTest, DevModeFallsBackWhenEnvVarUnset) {
    SecretKeyEnvScope envScope;  // unset
    auto helper = MakeSecretsAtRest(/*isProd=*/false);
    ASSERT_NE(helper, nullptr);
    EXPECT_EQ(helper->Decrypt(helper->Encrypt("dev-secret")), "dev-secret");
}

TEST(MakeSecretsAtRestTest, DevModeFallsBackWhenEnvVarEmpty) {
    SecretKeyEnvScope envScope;
    SetEnv(kSecretKeyEnvVar, "");
    auto helper = MakeSecretsAtRest(/*isProd=*/false);
    ASSERT_NE(helper, nullptr);
}

TEST(MakeSecretsAtRestTest, DevModeStillRejectsWrongLengthEnvVar) {
    // When the operator explicitly sets the env var, even in dev,
    // wrong-length should fail loudly. Otherwise an operator who
    // typo'd a real key would silently fall back to the dev key
    // and re-encrypt prod data with it.
    SecretKeyEnvScope envScope;
    Blob shortKey(16, std::byte{0});
    std::string encoded = Base64Encode(shortKey);
    SetEnv(kSecretKeyEnvVar, encoded.c_str());
    EXPECT_THROW(MakeSecretsAtRest(/*isProd=*/false), std::runtime_error);
}

TEST(MakeSecretsAtRestTest, ProdAndDevWithSameValidKeyDecryptInteroperably) {
    // Sanity: a value encrypted in dev mode with the explicit env
    // var should decrypt in prod mode with the same env var. This
    // is the deploy scenario where dev and prod share the key.
    SecretKeyEnvScope envScope;
    Blob key(crypto_secretbox_KEYBYTES, std::byte{0x99});
    std::string encoded = Base64Encode(key);
    SetEnv(kSecretKeyEnvVar, encoded.c_str());

    auto dev = MakeSecretsAtRest(/*isProd=*/false);
    auto prod = MakeSecretsAtRest(/*isProd=*/true);
    std::string ct = dev->Encrypt("shared");
    EXPECT_EQ(prod->Decrypt(ct), "shared");
}

// =====================================================================
// Phase 8.1c — Migration pass.
// =====================================================================

TEST(MigrateSecretsTest, EncryptsLegacyPlaintextRows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EncryptsLegacyPlaintextRows",
        [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TableHelpers::ConfigSecrets configSecrets(db);

        // Pre-seed plaintext rows directly via the table helper —
        // mimics the state immediately after PopulateConfigSecrets
        // (or any fresh database before the migration runs).
        configSecrets.AddSecret(tx, "key1", "plain1");
        configSecrets.AddSecret(tx, "key2", "plain2");

        auto atRest = std::make_shared<SecretsAtRest>(
            Blob(crypto_secretbox_KEYBYTES, std::byte{0x42}));

        int64_t rewritten = MigrateSecretsToEncrypted(tx, db, atRest);
        EXPECT_EQ(rewritten, 2);

        // Raw rows now hold ciphertext.
        std::string raw1 = configSecrets.LookupSecret(tx, "key1");
        std::string raw2 = configSecrets.LookupSecret(tx, "key2");
        EXPECT_TRUE(SecretsAtRest::IsEncrypted(raw1));
        EXPECT_TRUE(SecretsAtRest::IsEncrypted(raw2));
        EXPECT_EQ(raw1.find("plain1"), std::string::npos);
        EXPECT_EQ(raw2.find("plain2"), std::string::npos);

        // Decrypting them returns the original plaintext.
        EXPECT_EQ(atRest->Decrypt(raw1), "plain1");
        EXPECT_EQ(atRest->Decrypt(raw2), "plain2");
    });
}

TEST(MigrateSecretsTest, IdempotentSecondRunIsNoOp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IdempotentSecondRunIsNoOp",
        [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TableHelpers::ConfigSecrets configSecrets(db);
        configSecrets.AddSecret(tx, "key1", "plain1");

        auto atRest = std::make_shared<SecretsAtRest>(
            Blob(crypto_secretbox_KEYBYTES, std::byte{0x42}));

        EXPECT_EQ(MigrateSecretsToEncrypted(tx, db, atRest), 1);
        // Capture the post-first-migration ciphertext so we can
        // confirm the second run doesn't rewrite it.
        std::string firstCiphertext = configSecrets.LookupSecret(tx, "key1");

        // Second call sees v1: already and skips.
        EXPECT_EQ(MigrateSecretsToEncrypted(tx, db, atRest), 0);

        // The row's stored ciphertext is unchanged — re-running the
        // migration must not rotate the nonce or otherwise touch
        // encrypted rows.
        std::string secondCiphertext = configSecrets.LookupSecret(tx, "key1");
        EXPECT_EQ(firstCiphertext, secondCiphertext);
    });
}

TEST(MigrateSecretsTest, MixedRowsHandledCorrectly) {
    // Realistic mid-rollout state: some rows still plaintext, some
    // already encrypted (e.g., a partial deploy that crashed
    // halfway). The migration should encrypt the plaintext ones
    // and leave the encrypted ones untouched.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MixedRowsHandledCorrectly",
        [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TableHelpers::ConfigSecrets configSecrets(db);

        auto atRest = std::make_shared<SecretsAtRest>(
            Blob(crypto_secretbox_KEYBYTES, std::byte{0x42}));

        // 2 plaintext + 1 already-encrypted.
        configSecrets.AddSecret(tx, "plain_a", "value-a");
        configSecrets.AddSecret(tx, "plain_b", "value-b");
        std::string preEncrypted = atRest->Encrypt("value-c-already-encrypted");
        configSecrets.AddSecret(tx, "encrypted_c", preEncrypted);

        EXPECT_EQ(MigrateSecretsToEncrypted(tx, db, atRest), 2);

        // All three rows now decrypt to their original plaintext.
        EXPECT_EQ(atRest->Decrypt(configSecrets.LookupSecret(tx, "plain_a")),
                  "value-a");
        EXPECT_EQ(atRest->Decrypt(configSecrets.LookupSecret(tx, "plain_b")),
                  "value-b");
        EXPECT_EQ(atRest->Decrypt(configSecrets.LookupSecret(tx, "encrypted_c")),
                  "value-c-already-encrypted");

        // The pre-encrypted row's ciphertext is byte-for-byte
        // unchanged (nonce wasn't rotated).
        EXPECT_EQ(configSecrets.LookupSecret(tx, "encrypted_c"), preEncrypted);
    });
}

TEST(MigrateSecretsTest, NullSecretsAtRestIsNoOp) {
    // Defensive: if the caller passes a null helper (e.g., the
    // env var resolved to dev fallback during a test that skipped
    // wiring), don't crash — return 0 and leave the table alone.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("NullSecretsAtRestIsNoOp",
        [&](Transaction& tx) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        TableHelpers::ConfigSecrets configSecrets(db);
        configSecrets.AddSecret(tx, "key1", "plain1");

        EXPECT_EQ(MigrateSecretsToEncrypted(tx, db, nullptr), 0);
        EXPECT_EQ(configSecrets.LookupSecret(tx, "key1"), "plain1");
    });
}

}  // namespace
}  // namespace Secrets
