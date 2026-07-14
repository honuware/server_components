#include "secrets_helper.h"
#include "secrets_helper_test_util.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sodium.h>

#include "secrets_at_rest.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "test/src/util/test_helper.h"
#include "sql_util/table_helpers/config_secrets.h"

namespace Secrets {
namespace {

TEST(SecretsHelperTest, LookupSecretBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SecretsHelperTest.LookupSecretBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        TableHelpers::ConfigSecrets configSecrets(databaseHelper);
        configSecrets.AddSecret(transaction, "test1", "value1");
        configSecrets.AddSecret(transaction, "test2", "value2");

        SecretsHelperPtr secretsHelper = MakeSecretsHelper(databaseHelper);
        ASSERT_EQ(secretsHelper->LookupSecret(transaction, "test1"), "value1");
        ASSERT_EQ(secretsHelper->LookupSecret(transaction, "test2"), "value2");
        });
}

TEST(SecretsHelperTest, AddSecretBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SecretsHelperTest.AddSecretBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        SecretsHelperPtr secretsHelper = MakeSecretsHelper(databaseHelper);
        secretsHelper->AddSecret(transaction, "test1", "value1");
        secretsHelper->AddSecret(transaction, "test2", "value2");

        TableHelpers::ConfigSecrets configSecrets(databaseHelper);

        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test1"), "value1");
        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test2"), "value2");
        });
}

TEST(SecretsHelperTest, AddLookupSecretTestUtil) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SecretsHelperTest.AddLookupSecretTestUtil", [&](Transaction& transaction) {
        SecretsHelperPtr secretsHelper = Secrets::Test::MakeTestSecretsHelper();

        secretsHelper->AddSecret(transaction, "test1", "value1");
        secretsHelper->AddSecret(transaction, "test2", "value2");

        ASSERT_EQ(secretsHelper->LookupSecret(transaction, "test1"), "value1");
        ASSERT_EQ(secretsHelper->LookupSecret(transaction, "test2"), "value2");
        });
}

TEST(SecretsHelperTest, SchedulingConfigDefaultsLoaded) {
    auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SecretsHelperTest.SchedulingConfigDefaultsLoaded", [&](Transaction& transaction) {
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_lunch_threshold_minutes"), "300");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_lunch_length_minutes"), "30");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_min_buffer_minutes"), "10");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_setup_buffer_minutes"), "0");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_teardown_buffer_minutes"), "0");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "scheduling_walkin_min_buffer_minutes"), "15");
    });
}

// Classes Phase 8 §2.3 — staff check-in window + history defaults are seeded.
TEST(SecretsHelperTest, ClassCheckinConfigDefaultsLoaded) {
    auto secretsHelper = Secrets::Test::MakeTestSecretsHelper();
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SecretsHelperTest.ClassCheckinConfigDefaultsLoaded", [&](Transaction& transaction) {
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "class_checkin_window_before_minutes"), "60");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "class_checkin_window_after_minutes"), "180");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "class_checkin_history_weeks"), "4");
    });
}

// =====================================================================
// Phase 8.1 of the security review: at-rest encryption.
//
// When MakeSecretsHelper is called with a SecretsAtRest, the helper
// transparently encrypts on write and decrypts on read. Callers
// continue to see plaintext; the database column holds ciphertext.
// =====================================================================

namespace {

::Secrets::SecretsAtRestPtr MakeTestSecretsAtRest() {
    Blob key(crypto_secretbox_KEYBYTES, std::byte{0x42});
    return std::make_shared<::Secrets::SecretsAtRest>(key);
}

}  // namespace

TEST(SecretsHelperTest, AtRestEncryptsValueOnDisk) {
    // The plaintext must NOT appear in the underlying config_secrets
    // row when at-rest encryption is wired in.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "SecretsHelperTest.AtRestEncryptsValueOnDisk",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        auto atRest = MakeTestSecretsAtRest();
        auto secretsHelper = MakeSecretsHelper(databaseHelper, atRest);

        const std::string plain = "hunter2-do-not-leak";
        secretsHelper->AddSecret(transaction, "square_access_token", plain);

        // Read the raw row directly via the table helper (which does
        // NOT decrypt) — confirm the ciphertext is on disk and the
        // plaintext is absent.
        TableHelpers::ConfigSecrets configSecrets(databaseHelper);
        std::string raw = configSecrets.LookupSecret(
            transaction, "square_access_token");
        EXPECT_TRUE(::Secrets::SecretsAtRest::IsEncrypted(raw))
            << "AddSecret with at-rest helper must produce a v1: prefix";
        EXPECT_EQ(raw.find(plain), std::string::npos)
            << "raw row must NOT contain the plaintext anywhere";
    });
}

TEST(SecretsHelperTest, AtRestRoundTripsThroughLookup) {
    // Caller sees plaintext. The encryption is fully internal.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "SecretsHelperTest.AtRestRoundTripsThroughLookup",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        auto atRest = MakeTestSecretsAtRest();
        auto secretsHelper = MakeSecretsHelper(databaseHelper, atRest);

        secretsHelper->AddSecret(transaction, "k", "the-actual-value");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "k"),
                  "the-actual-value");
    });
}

TEST(SecretsHelperTest, AtRestPassthroughForLegacyPlaintext) {
    // Phase 8.1c migration window: rows seeded by
    // PopulateConfigSecrets are plaintext until the migration pass
    // re-encrypts them. The helper must continue to return
    // plaintext for those rows so the read path doesn't break
    // between database init and migration.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "SecretsHelperTest.AtRestPassthroughForLegacyPlaintext",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        // Seed a plaintext row directly via the table helper (mimics
        // PopulateConfigSecrets / pre-migration rows).
        TableHelpers::ConfigSecrets configSecrets(databaseHelper);
        configSecrets.AddSecret(transaction, "legacy_key", "legacy-plain-value");

        auto atRest = MakeTestSecretsAtRest();
        auto secretsHelper = MakeSecretsHelper(databaseHelper, atRest);
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "legacy_key"),
                  "legacy-plain-value");
    });
}

TEST(SecretsHelperTest, NullAtRestPreservesPlaintextBehavior) {
    // The seed-time bootstrap (PopulateConfigSecrets) constructs
    // the helper with secretsAtRest=nullptr. Confirm that path is
    // the pre-Phase-8 plaintext write/read.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "SecretsHelperTest.NullAtRestPreservesPlaintextBehavior",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        auto secretsHelper = MakeSecretsHelper(databaseHelper, /*atRest=*/nullptr);

        secretsHelper->AddSecret(transaction, "k", "plain-on-disk");

        TableHelpers::ConfigSecrets configSecrets(databaseHelper);
        std::string raw = configSecrets.LookupSecret(transaction, "k");
        EXPECT_EQ(raw, "plain-on-disk")
            << "with null SecretsAtRest the row must be the literal plaintext";
        EXPECT_FALSE(::Secrets::SecretsAtRest::IsEncrypted(raw));
    });
}

TEST(SecretsHelperTest, AtRestUsesFreshNonceAcrossWrites) {
    // Two AddSecret calls with the same plaintext must produce
    // different ciphertexts on disk — without per-write randomness
    // an attacker could correlate equal values across rows.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction(
        "SecretsHelperTest.AtRestUsesFreshNonceAcrossWrites",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        auto atRest = MakeTestSecretsAtRest();
        auto secretsHelper = MakeSecretsHelper(databaseHelper, atRest);

        secretsHelper->AddSecret(transaction, "k1", "duplicate-value");
        secretsHelper->AddSecret(transaction, "k2", "duplicate-value");

        TableHelpers::ConfigSecrets configSecrets(databaseHelper);
        std::string raw1 = configSecrets.LookupSecret(transaction, "k1");
        std::string raw2 = configSecrets.LookupSecret(transaction, "k2");
        EXPECT_NE(raw1, raw2)
            << "same plaintext must encrypt differently across writes";

        // Both still decrypt back to the same plaintext.
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "k1"),
                  "duplicate-value");
        EXPECT_EQ(secretsHelper->LookupSecret(transaction, "k2"),
                  "duplicate-value");
    });
}

} // namespace {
} // namespace Secrets {