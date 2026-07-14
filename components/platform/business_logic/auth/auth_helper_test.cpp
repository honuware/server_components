#include "auth_helper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <sodium.h>

#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"
#include "util/secrets/secrets_helper_test_util.h"


namespace Auth {
namespace {

// Most of the auth-helper tests use the explicit-parameter HashPassword
// overload at INTERACTIVE cost. This keeps each Argon2id call to ~50 ms
// instead of the ~250 ms MODERATE default — when there are dozens of these
// tests in the suite, the difference matters. The tests for
// HashPasswordWithSecrets exercise the secret-driven path explicitly.
TEST(AuthHelperTest, HashPasswordBasic) {
    AuthHelper helper;
    std::string password = "MySecretPassword123!";
    std::string hash = helper.HashPassword(
        password,
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE);
    EXPECT_FALSE(hash.empty());
    EXPECT_TRUE(helper.VerifyPassword(password, hash));
}

TEST(AuthHelperTest, HashPasswordInvalid) {
    AuthHelper helper;
    std::string password = "MySecretPassword123!";
    std::string wrongPassword = "WrongPassword456!";
    std::string hash = helper.HashPassword(
        password,
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE);
    EXPECT_FALSE(hash.empty());
    EXPECT_FALSE(helper.VerifyPassword(wrongPassword, hash));
}

TEST(AuthHelperTest, RandomBytesBasic) {
    AuthHelper helper;
    size_t length = 64;
    Blob bytes = helper.RandomBytes(length);
    EXPECT_EQ(bytes.size(), length);
}

TEST(AuthHelperTest, Base64EncodeBasic) {
    AuthHelper helper;
    std::string stringData = "hello world";
    std::string encoded = helper.Base64Encode(stringData);
    // "hello world" base64 encoded is "aGVsbG8gd29ybGQ="
    EXPECT_EQ(encoded, "aGVsbG8gd29ybGQ");
}

TEST(AuthHelperTest, Base64DecodeBasic) {
    AuthHelper helper;
    std::string stringData = "hello world";
    std::string encoded = helper.Base64Encode(stringData);
    // "hello world" base64 encoded is "aGVsbG8gd29ybGQ="
    EXPECT_EQ(encoded, "aGVsbG8gd29ybGQ");
    std::vector<std::byte> decodedBytes = helper.Base64Decode(encoded);
    std::string decodedString(reinterpret_cast<const char*>(decodedBytes.data()), decodedBytes.size());
    EXPECT_EQ(decodedString, stringData);
}

TEST(AuthHelperTest, HashBinaryBasic) {
    AuthHelper helper;

    // Construct two different blobs
    Blob blobA;
    for (char c : std::string("alpha")) {
        blobA.push_back(static_cast<std::byte>(c));
    }
    Blob blobB;
    for (char c : std::string("beta")) {
        blobB.push_back(static_cast<std::byte>(c));
    }
    Blob blobAdup = blobA; // identical to blobA

    std::string hashA1 = helper.HashBinary(blobA);
    std::string hashA2 = helper.HashBinary(blobAdup);
    std::string hashB = helper.HashBinary(blobB);

    // Deterministic for same input
    EXPECT_EQ(hashA1, hashA2);
    // Different inputs should differ
    EXPECT_NE(hashA1, hashB);
    // Hex length should be 2 * crypto_generichash_BYTES (usually 64 chars)
    EXPECT_EQ(hashA1.size(), crypto_generichash_BYTES * 2);
}

// --- Phase 2.1: ConstantTimeEqual ---

TEST(AuthHelperTest, ConstantTimeEqualMatch) {
    EXPECT_TRUE(AuthHelper::ConstantTimeEqual("hello", "hello"));
    EXPECT_TRUE(AuthHelper::ConstantTimeEqual(
        std::string(64, 'a'), std::string(64, 'a')));
}

TEST(AuthHelperTest, ConstantTimeEqualMismatch) {
    // Same length, different content.
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual("hello", "Hello"));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual("hello", "hellp"));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual(
        std::string(64, 'a'), std::string(64, 'b')));
    // Differ in just the last byte — exercises the don't-short-circuit
    // property even though we can't observe timing in a unit test.
    std::string a = "abcdefghijklmnop";
    std::string b = "abcdefghijklmnoq";
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual(a, b));
}

TEST(AuthHelperTest, ConstantTimeEqualLengthMismatch) {
    // Length mismatch returns false without consulting sodium_memcmp.
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual("", "x"));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual("x", ""));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual("hello", "hello "));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual(
        std::string(63, 'a'), std::string(64, 'a')));
}

TEST(AuthHelperTest, ConstantTimeEqualEmptyEqualsEmpty) {
    // Two empty inputs are equal; this is the boundary where we don't
    // call sodium_memcmp at all.
    EXPECT_TRUE(AuthHelper::ConstantTimeEqual("", ""));
}

TEST(AuthHelperTest, ConstantTimeEqualHandlesEmbeddedNull) {
    // Compare past an embedded NUL — a string-view comparison must not
    // stop at NUL characters or it'd false-positive on hashes that
    // happen to contain one.
    std::string a("ab\0cd", 5);
    std::string b("ab\0cd", 5);
    std::string c("ab\0ce", 5);
    EXPECT_TRUE(AuthHelper::ConstantTimeEqual(a, b));
    EXPECT_FALSE(AuthHelper::ConstantTimeEqual(a, c));
}

// --- Phase 2.5: HashPasswordWithSecrets ---

namespace {

// Parse the t= and m= cost parameters out of a libsodium Argon2id encoded
// hash string of the form `$argon2id$v=19$m=<mem>,t=<ops>,p=1$...`.
struct ParsedArgon2Cost {
    long m = 0;  // memory limit in KB (libsodium encodes m in KB)
    long t = 0;  // ops limit
};

ParsedArgon2Cost ParseArgon2Cost(const std::string& encoded) {
    // Format: "$argon2id$v=19$m=NNN,t=NN,p=1$..."
    ParsedArgon2Cost result;
    auto mPos = encoded.find("m=");
    auto tPos = encoded.find("t=");
    if (mPos != std::string::npos) {
        result.m = std::strtol(encoded.c_str() + mPos + 2, nullptr, 10);
    }
    if (tPos != std::string::npos) {
        result.t = std::strtol(encoded.c_str() + tPos + 2, nullptr, 10);
    }
    return result;
}

} // namespace

TEST(AuthHelperTest, HashPasswordRespectsExplicitCost) {
    // Direct check that the parameterized HashPassword plumbs ops/mem into
    // the encoded hash. Uses values larger than INTERACTIVE so we know the
    // override took effect.
    AuthHelper helper;
    const std::uint64_t ops = 3;
    const std::size_t memBytes = 32 * 1024 * 1024;  // 32 MB
    std::string hash = helper.HashPassword("hunter2", ops, memBytes);

    ParsedArgon2Cost cost = ParseArgon2Cost(hash);
    EXPECT_EQ(cost.t, static_cast<long>(ops));
    // libsodium encodes m in KB; our memBytes was 32 MB = 32768 KB.
    EXPECT_EQ(cost.m, static_cast<long>(memBytes / 1024));
}

TEST(AuthHelperTest, HashPasswordWithSecretsRespectsOpsLimitSecret) {
    // Phase 2.5: kAuthArgon2OpsLimit / kAuthArgon2MemLimitKb drive the
    // hash cost, and the encoded hash reflects them.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HashPasswordWithSecretsRespectsOpsLimitSecret",
        [&](Transaction& transaction) {
            auto secrets = Secrets::Test::MakeTestSecretsHelper();
            // Override the test defaults to use ops=3 and mem=16384 KB
            // (16 MB) — values distinct from both INTERACTIVE and
            // MODERATE so we know the secrets are what got plumbed.
            secrets->AddSecret(transaction, Secrets::kAuthArgon2OpsLimit, "3");
            secrets->AddSecret(transaction, Secrets::kAuthArgon2MemLimitKb, "16384");

            std::string hash = AuthHelper::HashPasswordWithSecrets(
                transaction, secrets, "hunter2");

            ParsedArgon2Cost cost = ParseArgon2Cost(hash);
            EXPECT_EQ(cost.t, 3);
            EXPECT_EQ(cost.m, 16384);

            // Sanity-check the hash is verifiable.
            AuthHelper helper;
            EXPECT_TRUE(helper.VerifyPassword("hunter2", hash));
        });
}

TEST(AuthHelperTest, HashPasswordWithSecretsFallsBackToModerateOnMissingSecrets) {
    // If the secrets store has no Argon2 entries (or an unparseable one),
    // we fall back to MODERATE rather than silently weakening the hash.
    // crypto_pwhash_OPSLIMIT_MODERATE is 3 and crypto_pwhash_MEMLIMIT_MODERATE
    // is 256 MB = 262144 KB.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HashPasswordWithSecretsFallsBackToModerateOnMissingSecrets",
        [&](Transaction& transaction) {
            auto secrets = Secrets::Test::MakeTestSecretsHelper();
            // Replace the test-helper's INTERACTIVE override with empty
            // values to simulate "secrets store reachable but Argon2
            // entries missing".
            secrets->AddSecret(transaction, Secrets::kAuthArgon2OpsLimit, "");
            secrets->AddSecret(transaction, Secrets::kAuthArgon2MemLimitKb, "");

            std::string hash = AuthHelper::HashPasswordWithSecrets(
                transaction, secrets, "hunter2");
            ParsedArgon2Cost cost = ParseArgon2Cost(hash);
            EXPECT_EQ(cost.t, static_cast<long>(crypto_pwhash_OPSLIMIT_MODERATE));
            EXPECT_EQ(cost.m,
                static_cast<long>(crypto_pwhash_MEMLIMIT_MODERATE / 1024));
        });
}

TEST(AuthHelperTest, HashPasswordWithSecretsFallsBackToModerateOnNullSecrets) {
    // Null secrets pointer also resolves to MODERATE — a
    // production-side caller that forgot to plumb the helper still
    // gets a strong hash, never INTERACTIVE.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HashPasswordWithSecretsFallsBackToModerateOnNullSecrets",
        [&](Transaction& transaction) {
            std::string hash = AuthHelper::HashPasswordWithSecrets(
                transaction, nullptr, "hunter2");
            ParsedArgon2Cost cost = ParseArgon2Cost(hash);
            EXPECT_EQ(cost.t, static_cast<long>(crypto_pwhash_OPSLIMIT_MODERATE));
            EXPECT_EQ(cost.m,
                static_cast<long>(crypto_pwhash_MEMLIMIT_MODERATE / 1024));
        });
}

TEST(AuthHelperTest, HashPasswordWithSecretsRespectsTestHelperInteractiveOverride) {
    // The test secrets helper sets ops=2 / mem=64MB (INTERACTIVE) so the
    // suite stays fast. This test makes that override explicit so a
    // future change to the override is caught here.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("HashPasswordWithSecretsRespectsTestHelperInteractiveOverride",
        [&](Transaction& transaction) {
            auto secrets = Secrets::Test::MakeTestSecretsHelper();
            std::string hash = AuthHelper::HashPasswordWithSecrets(
                transaction, secrets, "hunter2");
            ParsedArgon2Cost cost = ParseArgon2Cost(hash);
            EXPECT_EQ(cost.t, 2);
            EXPECT_EQ(cost.m, 65536);
        });
}

} // namespace
}  // namespace Auth {