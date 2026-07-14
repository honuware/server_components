#include "device_tokens.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/device_tokens.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

using ::testing::ElementsAre;

static int AddTestPerson(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view email) {
    People people(testDb.GetDatabaseHelper());
    return people.AddPerson(transaction, email, "First", "Last", "hash");
}

TEST(DeviceTokensTest, AddDeviceTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddDeviceTokenBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "user@example.com");

        int64_t oneHourUs = 60LL * 60LL * 1000000LL;
        int id = deviceTokens.AddDeviceToken(transaction, personId, "shash", oneHourUs);
        ASSERT_GT(id, 0);

        KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensPersonId)), StringFromInt(personId));
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensSecretHash)), "shash");
        int64_t expiresAt = std::stoll(row.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        EXPECT_GE(expiresAt, nowUs);
    });
}

TEST(DeviceTokensTest, AddDeviceTokenPersonNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddDeviceTokenPersonNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.AddDeviceToken(transaction, 999999, "x", 1000), std::exception);
    });
}

TEST(DeviceTokensTest, LookupDeviceTokenByIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenByIdBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "luid@example.com");

        int id = deviceTokens.AddDeviceToken(transaction, personId, "abc", 1000000);
        KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensId)), StringFromInt(id));
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensPersonId)), StringFromInt(personId));
    });
}

TEST(DeviceTokensTest, LookupDeviceTokenByIdNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenByIdNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenById(transaction, 424242), std::runtime_error);
    });
}

TEST(DeviceTokensTest, LookupDeviceTokenByUuidBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenByIdBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "luid@example.com");

        int id = deviceTokens.AddDeviceToken(transaction, personId, "abc", 1000000);
        KeyValueTable rowId = deviceTokens.LookupDeviceTokenById(transaction, id);
        KeyValueTable row = deviceTokens.LookupDeviceTokenByUuid(
            transaction, rowId[std::string(DbSchema::kDeviceTokensUuid)]);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensId)), StringFromInt(id));
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensPersonId)), StringFromInt(personId));
        });
}

TEST(DeviceTokensTest, LookupDeviceTokenByUuidNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenByIdNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenByUuid(transaction, "bad uuid"), std::runtime_error);
        });
}

TEST(DeviceTokensTest, LookupDeviceTokensByPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokensByPersonBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "lutp@example.com");

        (void)deviceTokens.AddDeviceToken(transaction, personId, "s1", 1000000);
        (void)deviceTokens.AddDeviceToken(transaction, personId, "s2", 1000000);

        KeyValueTableArray rows = deviceTokens.LookupDeviceTokensByPerson(transaction, personId);
        ASSERT_GE(rows.size(), 2u);
        for (const auto& kv : rows) {
            EXPECT_EQ(kv.at(std::string(DbSchema::kDeviceTokensPersonId)), StringFromInt(personId));
        }
    });
}

TEST(DeviceTokensTest, LookupDeviceTokensByPersonNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokensByPersonNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokensByPerson(transaction, 777777), std::runtime_error);
    });
}

TEST(DeviceTokensTest, LookupDeviceTokenBySecretHashBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenBySecretHashBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "luth@example.com");

        int id = deviceTokens.AddDeviceToken(transaction, personId, "hashy", 1000000);
        KeyValueTable row = deviceTokens.LookupDeviceTokenBySecretHash(transaction, "hashy");
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensId)), StringFromInt(id));
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensPersonId)), StringFromInt(personId));
    });
}

TEST(DeviceTokensTest, LookupDeviceTokenBySecretHashNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupDeviceTokenBySecretHashNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenBySecretHash(transaction, "missing"), std::runtime_error);
    });
}

TEST(DeviceTokensTest, IsValidBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsValidBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "valid@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 60LL * 1000000LL);
        EXPECT_TRUE(deviceTokens.IsValid(transaction, id));
    });
}

TEST(DeviceTokensTest, IsValidNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsValidNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.IsValid(transaction, 123456), std::runtime_error);
    });
}

TEST(DeviceTokensTest, IsValidRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsValidRevoked", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rev@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 1000000);
        deviceTokens.Revoke(transaction, id);
        EXPECT_FALSE(deviceTokens.IsValid(transaction, id));
    });
}

TEST(DeviceTokensTest, IsValidExpired) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsValidExpired", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "exp@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 1000000);

        // Force expiration
        KeyValueTable kv = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "1" }
        };
        DbCrud::UpdateRow(
            transaction,
            testDb.GetDatabaseHelper(),
            DbSchema::kDeviceTokensTable,
            DbSchema::kDeviceTokensId,
            StringFromInt(id),
            kv);

        EXPECT_FALSE(deviceTokens.IsValid(transaction, id));
    });
}

TEST(DeviceTokensTest, RevokeBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RevokeBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rev2@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 1000000);
        deviceTokens.Revoke(transaction, id);

        KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensRevoked)), "t");
        EXPECT_FALSE(deviceTokens.IsValid(transaction, id));
    });
}

TEST(DeviceTokensTest, RevokeNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RevokeNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW(deviceTokens.Revoke(transaction, 999999), std::runtime_error);
    });
}

TEST(DeviceTokensTest, UpdateDeviceTokenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateDeviceTokenBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "upd@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "oldh", 1000000);

        KeyValueTable before = deviceTokens.LookupDeviceTokenById(transaction, id);
        std::string beforeUsed = before.at(std::string(DbSchema::kDeviceTokensLastUsedAt));
        std::string beforeUuid = before.at(std::string(DbSchema::kDeviceTokensUuid));

        bool ok = deviceTokens.UpdateDeviceToken(transaction, id, "newh", 2000000);
        EXPECT_TRUE(ok);

        KeyValueTable after = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(after.at(std::string(DbSchema::kDeviceTokensSecretHash)), "newh");
        int64_t expiresAt = std::stoll(after.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        EXPECT_GE(expiresAt, nowUs);
        EXPECT_NE(beforeUsed, after.at(std::string(DbSchema::kDeviceTokensLastUsedAt)));
        EXPECT_NE(beforeUuid, after.at(std::string(DbSchema::kDeviceTokensUuid)));
    });
}

TEST(DeviceTokensTest, UpdateDeviceTokenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateDeviceTokenNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)deviceTokens.UpdateDeviceToken(transaction, 99999, "any", 1000), std::runtime_error);
    });
}

TEST(DeviceTokensTest, UpdateDeviceTokenExpired) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateDeviceTokenExpired", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "updexp@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "oldh", 1000000);

        KeyValueTable orig = deviceTokens.LookupDeviceTokenById(transaction, id);

        // Force expiration
        KeyValueTable kv = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "1" }
        };
        DbCrud::UpdateRow(
            transaction,
            testDb.GetDatabaseHelper(),
            DbSchema::kDeviceTokensTable,
            DbSchema::kDeviceTokensId,
            StringFromInt(id),
            kv);

        bool ok = deviceTokens.UpdateDeviceToken(transaction, id, "newh", 2000000);
        EXPECT_FALSE(ok);

        KeyValueTable after = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(after.at(std::string(DbSchema::kDeviceTokensSecretHash)), orig.at(std::string(DbSchema::kDeviceTokensSecretHash)));
        EXPECT_EQ(after.at(std::string(DbSchema::kDeviceTokensLastUsedAt)), orig.at(std::string(DbSchema::kDeviceTokensLastUsedAt)));
    });
}

TEST(DeviceTokensTest, UpdateDeviceTokenRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateDeviceTokenRevoked", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "updrv@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "oldh", 1000000);

        deviceTokens.Revoke(transaction, id);
        KeyValueTable orig = deviceTokens.LookupDeviceTokenById(transaction, id);

        bool ok = deviceTokens.UpdateDeviceToken(transaction, id, "newh", 2000000);
        EXPECT_FALSE(ok);

        KeyValueTable after = deviceTokens.LookupDeviceTokenById(transaction, id);
        EXPECT_EQ(after.at(std::string(DbSchema::kDeviceTokensSecretHash)), orig.at(std::string(DbSchema::kDeviceTokensSecretHash)));
        EXPECT_EQ(after.at(std::string(DbSchema::kDeviceTokensLastUsedAt)), orig.at(std::string(DbSchema::kDeviceTokensLastUsedAt)));
    });
}

TEST(DeviceTokensTest, RemoveDeviceTokenByIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveDeviceTokenByIdBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rmid@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 1000000);

        deviceTokens.RemoveDeviceTokenById(transaction, id);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM device_tokens WHERE id = $1", StringFromInt(id));
        EXPECT_EQ(countStr, "0");
    });
}

TEST(DeviceTokensTest, RemoveDeviceTokenByIdNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveDeviceTokenByIdNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        deviceTokens.RemoveDeviceTokenById(transaction, 999999); // should not throw
    });
}

TEST(DeviceTokensTest, RemoveDeviceTokensForUserBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveDeviceTokensForUserBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rmuser@example.com");

        (void)deviceTokens.AddDeviceToken(transaction, personId, "h1", 1000000);
        (void)deviceTokens.AddDeviceToken(transaction, personId, "h2", 1000000);

        deviceTokens.RemoveDeviceTokensForUser(transaction, personId);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM device_tokens WHERE person_id = $1", StringFromInt(personId));
        EXPECT_EQ(countStr, "0");
    });
}

TEST(DeviceTokensTest, RemoveDeviceTokensForUserNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveDeviceTokensForUserNotFound", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        deviceTokens.RemoveDeviceTokensForUser(transaction, 123456); // should not throw
    });
}

TEST(DeviceTokensTest, DeleteExpiredDeletesOnlyPastRows) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredDeletesOnlyPastRows", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "delexp@example.com");

        int futureId = deviceTokens.AddDeviceToken(transaction, personId, "future", 60LL * 1000000LL);
        int pastId1 = deviceTokens.AddDeviceToken(transaction, personId, "past1", 1000000);
        int pastId2 = deviceTokens.AddDeviceToken(transaction, personId, "past2", 1000000);

        // Force two rows into the past.
        KeyValueTable kvPast = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "1" }
        };
        DbCrud::UpdateRow(transaction, testDb.GetDatabaseHelper(),
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(pastId1), kvPast);
        DbCrud::UpdateRow(transaction, testDb.GetDatabaseHelper(),
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(pastId2), kvPast);

        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        int64_t deleted = deviceTokens.DeleteExpired(transaction, nowUs);
        EXPECT_EQ(deleted, 2);

        // Future row still present.
        KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, futureId);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensId)), StringFromInt(futureId));

        // Past rows are gone.
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenById(transaction, pastId1), std::runtime_error);
        EXPECT_THROW((void)deviceTokens.LookupDeviceTokenById(transaction, pastId2), std::runtime_error);
    });
}

TEST(DeviceTokensTest, DeleteExpiredNoExpiredRowsReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredNoExpiredRowsReturnsZero", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "noexp@example.com");
        (void)deviceTokens.AddDeviceToken(transaction, personId, "fresh", 60LL * 1000000LL);

        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        int64_t deleted = deviceTokens.DeleteExpired(transaction, nowUs);
        EXPECT_EQ(deleted, 0);
    });
}

TEST(DeviceTokensTest, DeleteExpiredBoundaryNotInclusive) {
    // Rows where expires_at == asOfUs should NOT be deleted (strictly less than).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredBoundaryNotInclusive", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "bdry@example.com");
        int id = deviceTokens.AddDeviceToken(transaction, personId, "h", 1000000);

        // Set expires_at to a known fixed value, then call DeleteExpired with the same value.
        KeyValueTable kv = {
            { std::string(DbSchema::kDeviceTokensExpiresAt), "1000" }
        };
        DbCrud::UpdateRow(transaction, testDb.GetDatabaseHelper(),
            DbSchema::kDeviceTokensTable, DbSchema::kDeviceTokensId,
            StringFromInt(id), kv);

        int64_t deleted = deviceTokens.DeleteExpired(transaction, 1000);
        EXPECT_EQ(deleted, 0);

        // One microsecond past the boundary deletes it.
        deleted = deviceTokens.DeleteExpired(transaction, 1001);
        EXPECT_EQ(deleted, 1);
    });
}

// --- Phase 2.4: ConsumeAndRotate ---

namespace {

// Helper: insert a token row with a known secret_hash and return the
// (id, uuid) so tests can call ConsumeAndRotate against it.
struct TokenRow {
    int64_t id = 0;
    std::string uuid;
};

TokenRow AddTokenAndFetchUuid(
    Transaction& transaction,
    DeviceTokens& deviceTokens,
    int64_t personId,
    std::string_view secretHash,
    int64_t microsUntilExpires) {
    TokenRow result;
    result.id = deviceTokens.AddDeviceToken(
        transaction, personId, secretHash, microsUntilExpires);
    KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, result.id);
    result.uuid = row.at(std::string(DbSchema::kDeviceTokensUuid));
    return result;
}

} // namespace

TEST(DeviceTokensTest, ConsumeAndRotateBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateBasic", [&](Transaction& transaction) {
        DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rotate-basic@example.com");
        TokenRow before = AddTokenAndFetchUuid(
            transaction, deviceTokens, personId, "old-hash",
            60LL * 60LL * 1000000LL);

        int64_t outPersonId = 0;
        std::string outNewUuid;
        int64_t newExpires = 90LL * 60LL * 1000000LL;
        bool ok = deviceTokens.ConsumeAndRotate(
            transaction, "old-hash", before.uuid, "new-hash",
            newExpires, outPersonId, outNewUuid);

        ASSERT_TRUE(ok);
        EXPECT_EQ(outPersonId, personId);
        // UUID must have rotated.
        EXPECT_NE(outNewUuid, before.uuid);
        EXPECT_FALSE(outNewUuid.empty());

        // The row in the DB now carries the new secret_hash and the new
        // uuid; the old secret_hash must no longer match anything.
        KeyValueTable row = deviceTokens.LookupDeviceTokenById(
            transaction, before.id);
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensSecretHash)), "new-hash");
        EXPECT_EQ(row.at(std::string(DbSchema::kDeviceTokensUuid)), outNewUuid);
        // last_used_at and expires_at advanced.
        EXPECT_FALSE(row.at(std::string(DbSchema::kDeviceTokensLastUsedAt)).empty());
        int64_t nowUs = std::stoll(
            transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        int64_t expiresAt = std::stoll(
            row.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        EXPECT_GT(expiresAt, nowUs);
    });
}

TEST(DeviceTokensTest, ConsumeAndRotateUnknownSecretHash) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateUnknownSecretHash",
        [&](Transaction& transaction) {
            DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
            int personId = AddTestPerson(
                transaction, testDb, "unknown-hash@example.com");
            TokenRow row = AddTokenAndFetchUuid(
                transaction, deviceTokens, personId, "real-hash",
                60LL * 60LL * 1000000LL);

            int64_t outPersonId = 0;
            std::string outNewUuid;
            bool ok = deviceTokens.ConsumeAndRotate(
                transaction, "wrong-hash", row.uuid, "new-hash",
                60LL * 1000000LL, outPersonId, outNewUuid);

            EXPECT_FALSE(ok);
            EXPECT_EQ(outPersonId, 0);
            EXPECT_TRUE(outNewUuid.empty());
            // Source row untouched.
            KeyValueTable existing = deviceTokens.LookupDeviceTokenById(
                transaction, row.id);
            EXPECT_EQ(
                existing.at(std::string(DbSchema::kDeviceTokensSecretHash)),
                "real-hash");
            EXPECT_EQ(
                existing.at(std::string(DbSchema::kDeviceTokensUuid)),
                row.uuid);
        });
}

TEST(DeviceTokensTest, ConsumeAndRotateUuidMismatch) {
    // Phase 2.4: matching the secret hash is not enough — the supplied
    // uuid (the public side of the cookie) must also match the row.
    // This catches a stale cookie attempting to rotate using a captured
    // hash with the wrong uuid.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateUuidMismatch",
        [&](Transaction& transaction) {
            DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
            int personId = AddTestPerson(
                transaction, testDb, "uuid-mismatch@example.com");
            TokenRow row = AddTokenAndFetchUuid(
                transaction, deviceTokens, personId, "real-hash",
                60LL * 60LL * 1000000LL);

            int64_t outPersonId = 0;
            std::string outNewUuid;
            // Same hash, fake uuid — must fail.
            bool ok = deviceTokens.ConsumeAndRotate(
                transaction, "real-hash",
                "00000000-0000-0000-0000-000000000000",
                "new-hash",
                60LL * 1000000LL, outPersonId, outNewUuid);

            EXPECT_FALSE(ok);
            EXPECT_EQ(outPersonId, 0);
            EXPECT_TRUE(outNewUuid.empty());
            // Row untouched.
            KeyValueTable existing = deviceTokens.LookupDeviceTokenById(
                transaction, row.id);
            EXPECT_EQ(
                existing.at(std::string(DbSchema::kDeviceTokensSecretHash)),
                "real-hash");
        });
}

TEST(DeviceTokensTest, ConsumeAndRotateRevoked) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateRevoked",
        [&](Transaction& transaction) {
            DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
            int personId = AddTestPerson(
                transaction, testDb, "rotate-revoked@example.com");
            TokenRow row = AddTokenAndFetchUuid(
                transaction, deviceTokens, personId, "revoked-hash",
                60LL * 60LL * 1000000LL);
            deviceTokens.Revoke(transaction, row.id);

            int64_t outPersonId = 0;
            std::string outNewUuid;
            bool ok = deviceTokens.ConsumeAndRotate(
                transaction, "revoked-hash", row.uuid, "new-hash",
                60LL * 1000000LL, outPersonId, outNewUuid);

            EXPECT_FALSE(ok);
            EXPECT_EQ(outPersonId, 0);
            EXPECT_TRUE(outNewUuid.empty());
            // Row's hash and uuid stay as they were.
            KeyValueTable existing = deviceTokens.LookupDeviceTokenById(
                transaction, row.id);
            EXPECT_EQ(
                existing.at(std::string(DbSchema::kDeviceTokensSecretHash)),
                "revoked-hash");
            EXPECT_EQ(
                existing.at(std::string(DbSchema::kDeviceTokensUuid)),
                row.uuid);
        });
}

TEST(DeviceTokensTest, ConsumeAndRotateExpired) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateExpired",
        [&](Transaction& transaction) {
            DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
            int personId = AddTestPerson(
                transaction, testDb, "rotate-expired@example.com");
            TokenRow row = AddTokenAndFetchUuid(
                transaction, deviceTokens, personId, "expired-hash",
                60LL * 60LL * 1000000LL);

            // Force the row's expiration into the past.
            KeyValueTable kvUpdate = {
                { std::string(DbSchema::kDeviceTokensExpiresAt), "1" }
            };
            DbCrud::UpdateRow(
                transaction, testDb.GetDatabaseHelper(),
                DbSchema::kDeviceTokensTable,
                DbSchema::kDeviceTokensId,
                std::to_string(row.id), kvUpdate);

            int64_t outPersonId = 0;
            std::string outNewUuid;
            bool ok = deviceTokens.ConsumeAndRotate(
                transaction, "expired-hash", row.uuid, "new-hash",
                60LL * 1000000LL, outPersonId, outNewUuid);

            EXPECT_FALSE(ok);
            EXPECT_EQ(outPersonId, 0);
            EXPECT_TRUE(outNewUuid.empty());
        });
}

TEST(DeviceTokensTest, ConsumeAndRotateContentionOnlyOneWins) {
    // Phase 2.4 of the security review: two attempts to rotate the
    // same (secret_hash, uuid) pair — only the first can succeed,
    // because the first UPDATE rotates both columns atomically. The
    // second sees no matching row and fails. This proves the SQL-level
    // atomicity of the WHERE clause; truly concurrent execution
    // (two pqxx connections in different threads) would block on the
    // row lock and serialize to the same outcome.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ConsumeAndRotateContentionOnlyOneWins",
        [&](Transaction& transaction) {
            DeviceTokens deviceTokens(testDb.GetDatabaseHelper());
            int personId = AddTestPerson(
                transaction, testDb, "rotate-contention@example.com");
            TokenRow row = AddTokenAndFetchUuid(
                transaction, deviceTokens, personId, "shared-hash",
                60LL * 60LL * 1000000LL);

            int64_t outPersonId1 = 0;
            std::string outNewUuid1;
            bool ok1 = deviceTokens.ConsumeAndRotate(
                transaction, "shared-hash", row.uuid, "new-hash-A",
                60LL * 1000000LL, outPersonId1, outNewUuid1);
            ASSERT_TRUE(ok1);

            int64_t outPersonId2 = 0;
            std::string outNewUuid2;
            bool ok2 = deviceTokens.ConsumeAndRotate(
                transaction, "shared-hash", row.uuid, "new-hash-B",
                60LL * 1000000LL, outPersonId2, outNewUuid2);
            EXPECT_FALSE(ok2);
            EXPECT_EQ(outPersonId2, 0);
            EXPECT_TRUE(outNewUuid2.empty());

            // Final row state reflects only the first rotation.
            KeyValueTable final_row = deviceTokens.LookupDeviceTokenById(
                transaction, row.id);
            EXPECT_EQ(
                final_row.at(std::string(DbSchema::kDeviceTokensSecretHash)),
                "new-hash-A");
            EXPECT_EQ(
                final_row.at(std::string(DbSchema::kDeviceTokensUuid)),
                outNewUuid1);
        });
}

} // namespace
} // namespace TableHelpers