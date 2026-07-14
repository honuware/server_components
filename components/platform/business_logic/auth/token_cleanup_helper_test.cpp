#include "token_cleanup_helper.h"

#include <gtest/gtest.h>

#include "db_schema/device_tokens.h"
#include "db_schema/email_verifications.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/device_tokens.h"
#include "sql_util/table_helpers/email_verifications.h"
#include "sql_util/table_helpers/people.h"
#include "test/src/util/database_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/types.h"

namespace Auth {
namespace {

static int AddTestPerson(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    std::string_view email) {
    TableHelpers::People people(testDb.GetDatabaseHelper());
    return people.AddPerson(transaction, email, "First", "Last", "hash");
}

static Secrets::SecretsHelperPtr MakeFilledSecrets(Transaction& transaction) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    secrets->AddSecret(transaction,
        Secrets::kEmailVerificationExpirationWindowInMicros,
        std::to_string(3600LL * 1000000LL));
    secrets->AddSecret(transaction,
        Secrets::kEmailVerificationAttemptLimit, "3");
    return secrets;
}

TEST(TokenCleanupHelperTest, CleanupExpiredDeletesBothTablesAndReturnsCounts) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CleanupExpiredDeletesBothTablesAndReturnsCounts",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        auto secrets = MakeFilledSecrets(transaction);
        TableHelpers::DeviceTokens deviceTokens(databaseHelper);
        TableHelpers::EmailVerifications emailVerifications(databaseHelper, secrets);

        int p1 = AddTestPerson(transaction, testDb, "tcu1@example.com");
        int p2 = AddTestPerson(transaction, testDb, "tcu2@example.com");
        int p3 = AddTestPerson(transaction, testDb, "tcu3@example.com");

        // Two expired device tokens, one fresh.
        int dtFresh = deviceTokens.AddDeviceToken(transaction, p1, "fresh", 60LL * 1000000LL);
        int dtPast1 = deviceTokens.AddDeviceToken(transaction, p2, "past1", 1000000);
        int dtPast2 = deviceTokens.AddDeviceToken(transaction, p3, "past2", 1000000);

        KeyValueTable expirePast = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "1" }
        };
        DbCrud::UpdateRow(transaction, databaseHelper,
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(dtPast1), expirePast);
        DbCrud::UpdateRow(transaction, databaseHelper,
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(dtPast2), expirePast);

        // One expired email verification, one fresh.
        int evFresh = emailVerifications.AddEmailVerificationById(transaction, p1, "freshtok");
        int evPast = emailVerifications.AddEmailVerificationById(transaction, p2, "pasttok");

        KeyValueTable evExpirePast = {
            { std::string(DbSchema::kEmailVerificationsExpiresAt), "1" }
        };
        DbCrud::UpdateRow(transaction, databaseHelper,
            DbSchema::kEmailVerificationsTable, DbSchema::kEmailVerificationsId,
            std::to_string(evPast), evExpirePast);

        TokenCleanupHelper helper(databaseHelper);
        TokenCleanupResult result = helper.CleanupExpired(transaction);

        EXPECT_EQ(result.deviceTokensDeleted, 2);
        EXPECT_EQ(result.emailVerificationsDeleted, 1);

        // Fresh rows survive.
        (void)deviceTokens.LookupDeviceTokenById(transaction, dtFresh);
        (void)emailVerifications.GetEmailVerificationInfo(transaction, evFresh);

        // Expired rows are gone.
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenById(transaction, dtPast1), std::runtime_error);
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenById(transaction, dtPast2), std::runtime_error);
        EXPECT_THROW(
            (void)emailVerifications.GetEmailVerificationInfo(transaction, evPast),
            std::runtime_error);
    });
}

TEST(TokenCleanupHelperTest, CleanupExpiredEmptyDatabaseReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CleanupExpiredEmptyDatabaseReturnsZero",
        [&](Transaction& transaction) {
        TokenCleanupHelper helper(testDb.GetDatabaseHelper());
        TokenCleanupResult result = helper.CleanupExpired(transaction);
        EXPECT_EQ(result.deviceTokensDeleted, 0);
        EXPECT_EQ(result.emailVerificationsDeleted, 0);
    });
}

TEST(TokenCleanupHelperTest, CleanupExpiredHonorsExplicitAsOfUs) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CleanupExpiredHonorsExplicitAsOfUs",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        TableHelpers::DeviceTokens deviceTokens(databaseHelper);

        int p = AddTestPerson(transaction, testDb, "asof@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, p, "h", 1000000);

        KeyValueTable kv = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "5000" }
        };
        DbCrud::UpdateRow(transaction, databaseHelper,
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(id), kv);

        TokenCleanupHelper helper(databaseHelper);

        // asOfUs strictly less than expires_at — nothing to delete.
        TokenCleanupResult before = helper.CleanupExpired(transaction, 4999);
        EXPECT_EQ(before.deviceTokensDeleted, 0);

        // asOfUs strictly greater than expires_at — row gets deleted.
        TokenCleanupResult after = helper.CleanupExpired(transaction, 5001);
        EXPECT_EQ(after.deviceTokensDeleted, 1);
    });
}

}  // namespace
}  // namespace Auth
