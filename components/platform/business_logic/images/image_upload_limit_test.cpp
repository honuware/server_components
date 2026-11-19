#include "image_upload_limit.h"

#include <stdexcept>

#include <gtest/gtest.h>

#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

// The upload limit's default, range and storage (CommunityFinder CM3 Phase 2
// §13.1).
namespace Images {
namespace {

TEST(ImageUploadLimitTest, TheDefaultIsTheSeededTenMegabytes) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadLimitDefault", [&](Transaction& transaction) {
        // The test double carries the framework's seeded secret values, so
        // this also keeps the constant and secret_values.cpp in step.
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        EXPECT_EQ(LoadImageUploadLimit(transaction, secrets), 10 * 1024 * 1024);
        EXPECT_EQ(kImageUploadLimitDefaultBytes, 10485760);
    });
}

TEST(ImageUploadLimitTest, AStoredLimitIsReadAsTheUploadRoutesWouldEnforceIt) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadLimitStored", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecretTest(Secrets::kImageMaxUploadBytes, "20971520");
        EXPECT_EQ(LoadImageUploadLimit(transaction, secrets), 20971520);
        // Set by hand outside the range: still what's enforced, so still what's read.
        secrets->AddSecretTest(Secrets::kImageMaxUploadBytes, "209715200");
        EXPECT_EQ(LoadImageUploadLimit(transaction, secrets), 209715200);
    });
}

TEST(ImageUploadLimitTest, AnUnsetOrUnreadableLimitReadsAsTheDefault) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadLimitUnreadable", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        for (const char* stored : {"", "ten", "10MB", "-5", "0", "1.5"}) {
            secrets->AddSecretTest(Secrets::kImageMaxUploadBytes, stored);
            EXPECT_EQ(LoadImageUploadLimit(transaction, secrets), kImageUploadLimitDefaultBytes)
                << "stored \"" << stored << "\"";
        }
    });
}

TEST(ImageUploadLimitTest, OnlyOneToFiftyMegabytesIsAllowed) {
    EXPECT_EQ(ValidateImageUploadLimit(1 * 1024 * 1024), "");
    EXPECT_EQ(ValidateImageUploadLimit(10 * 1024 * 1024), "");
    EXPECT_EQ(ValidateImageUploadLimit(50 * 1024 * 1024), "");
    EXPECT_EQ(ValidateImageUploadLimit(1 * 1024 * 1024 - 1),
              "The upload limit must be from 1 to 50 MB.");
    EXPECT_EQ(ValidateImageUploadLimit(50 * 1024 * 1024 + 1),
              "The upload limit must be from 1 to 50 MB.");
    EXPECT_NE(ValidateImageUploadLimit(0), "");
    EXPECT_NE(ValidateImageUploadLimit(-1), "");
}

TEST(ImageUploadLimitTest, SavingStoresTheLimitAndRefusesOneOutOfRange) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UploadLimitSave", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        SaveImageUploadLimit(transaction, secrets, 25 * 1024 * 1024);
        EXPECT_EQ(secrets->LookupSecretTest(Secrets::kImageMaxUploadBytes), "26214400");
        EXPECT_EQ(LoadImageUploadLimit(transaction, secrets), 26214400);

        EXPECT_THROW(SaveImageUploadLimit(transaction, secrets, 51 * 1024 * 1024),
                     std::invalid_argument);
        EXPECT_EQ(secrets->LookupSecretTest(Secrets::kImageMaxUploadBytes), "26214400");
    });
}

}  // namespace
}  // namespace Images
