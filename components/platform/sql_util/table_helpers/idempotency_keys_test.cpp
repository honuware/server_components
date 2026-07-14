#include "sql_util/table_helpers/idempotency_keys.h"

#include <gtest/gtest.h>

#include "db_schema/idempotency_keys.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"

namespace TableHelpers {
namespace {

// Current time for tests (in microseconds)
constexpr int64_t kTestCurrentTimeUs = 1000000000000LL;  // Some fixed point in time
constexpr int64_t kTestExpiresUs = kTestCurrentTimeUs + 172800000000LL;  // +48 hours

// AddIdempotencyKey
TEST(IdempotencyKeysTest, AddIdempotencyKeyBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddIdempotencyKeyBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_abc", "/api/purchase_pay_card",
            "hash123", "pending", "", kTestExpiresUs);
        ASSERT_GT(id, 0);

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysScope)], "user:123");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysKey)], "req_abc");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysEndpoint)], "/api/purchase_pay_card");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysRequestHash)], "hash123");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysStatus)], "pending");
        EXPECT_TRUE(row[std::string(DbSchema::kIdempotencyKeysResponseJson)].empty());
    });
}

TEST(IdempotencyKeysTest, AddIdempotencyKeyWithResponse) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddIdempotencyKeyWithResponse", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_xyz", "/api/purchase_pay_card",
            "", "completed", R"({"success": true})", kTestExpiresUs);

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysStatus)], "completed");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysResponseJson)], R"({"success": true})");
        EXPECT_TRUE(row[std::string(DbSchema::kIdempotencyKeysRequestHash)].empty());
    });
}

TEST(IdempotencyKeysTest, AddIdempotencyKeyDuplicateScopeKeyEndpoint) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddIdempotencyKeyDuplicateScopeKeyEndpoint", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        keys.AddIdempotencyKey(
            transaction, "user:123", "req_dup", "/api/endpoint",
            "", "pending", "", kTestExpiresUs);

        try {
            keys.AddIdempotencyKey(
                transaction, "user:123", "req_dup", "/api/endpoint",
                "", "pending", "", kTestExpiresUs);
            FAIL() << "Expected exception on duplicate scope+key+endpoint";
        } catch (const std::exception& e) {
            std::string msg = e.what();
            EXPECT_TRUE(msg.find("duplicate") != std::string::npos ||
                        msg.find("unique") != std::string::npos ||
                        msg.find("constraint") != std::string::npos);
        }
    });
}

TEST(IdempotencyKeysTest, AddIdempotencyKeySameKeyDifferentEndpoint) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddIdempotencyKeySameKeyDifferentEndpoint", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id1 = keys.AddIdempotencyKey(
            transaction, "user:123", "req_same", "/api/endpoint1",
            "", "pending", "", kTestExpiresUs);
        int64_t id2 = keys.AddIdempotencyKey(
            transaction, "user:123", "req_same", "/api/endpoint2",
            "", "pending", "", kTestExpiresUs);

        EXPECT_NE(id1, id2);
    });
}

// GetIdempotencyKey
TEST(IdempotencyKeysTest, GetIdempotencyKeyNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetIdempotencyKeyNotFound", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        KeyValueTable row = keys.GetIdempotencyKey(transaction, 999999);
        EXPECT_TRUE(row.empty());
    });
}

// GetIdempotencyKeyByScopeKeyEndpoint
TEST(IdempotencyKeysTest, GetIdempotencyKeyByScopeKeyEndpointBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetIdempotencyKeyByScopeKeyEndpointBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:456", "req_lookup", "/api/test",
            "", "completed", "", kTestExpiresUs);

        KeyValueTable row = keys.GetIdempotencyKeyByScopeKeyEndpoint(
            transaction, "user:456", "req_lookup", "/api/test");
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysId)], std::to_string(id));
    });
}

TEST(IdempotencyKeysTest, GetIdempotencyKeyByScopeKeyEndpointNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetIdempotencyKeyByScopeKeyEndpointNotFound", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        KeyValueTable row = keys.GetIdempotencyKeyByScopeKeyEndpoint(
            transaction, "user:999", "nonexistent", "/api/test");
        EXPECT_TRUE(row.empty());
    });
}

// GetIdempotencyKeysByScope
TEST(IdempotencyKeysTest, GetIdempotencyKeysByScopeBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetIdempotencyKeysByScopeBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        keys.AddIdempotencyKey(transaction, "user:100", "req_1", "/api/a", "", "pending", "", kTestExpiresUs);
        keys.AddIdempotencyKey(transaction, "user:100", "req_2", "/api/b", "", "completed", "", kTestExpiresUs);
        keys.AddIdempotencyKey(transaction, "user:200", "req_3", "/api/c", "", "pending", "", kTestExpiresUs);

        int64_t totalCount = 0;
        KeyValueTableArray rows = keys.GetIdempotencyKeysByScope(transaction, "user:100", 0, 0, &totalCount);
        EXPECT_EQ(rows.size(), 2u);
        EXPECT_EQ(totalCount, 2);
    });
}

// GetIdempotencyKeysByStatus
TEST(IdempotencyKeysTest, GetIdempotencyKeysByStatusBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetIdempotencyKeysByStatusBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        keys.AddIdempotencyKey(transaction, "user:1", "req_1", "/api/a", "", "pending", "", kTestExpiresUs);
        keys.AddIdempotencyKey(transaction, "user:2", "req_2", "/api/b", "", "completed", "", kTestExpiresUs);
        keys.AddIdempotencyKey(transaction, "user:3", "req_3", "/api/c", "", "pending", "", kTestExpiresUs);

        int64_t totalCount = 0;
        KeyValueTableArray rows = keys.GetIdempotencyKeysByStatus(transaction, "pending", 0, 0, &totalCount);
        EXPECT_EQ(rows.size(), 2u);
        EXPECT_EQ(totalCount, 2);
    });
}

// GetExpiredIdempotencyKeys
TEST(IdempotencyKeysTest, GetExpiredIdempotencyKeysBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetExpiredIdempotencyKeysBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t pastTime = kTestCurrentTimeUs - 1000000LL;  // Expired
        int64_t futureTime = kTestCurrentTimeUs + 1000000LL;  // Not expired

        IdempotencyKeys keys(databaseHelper);
        keys.AddIdempotencyKey(transaction, "user:1", "req_1", "/api/a", "", "completed", "", pastTime);
        keys.AddIdempotencyKey(transaction, "user:2", "req_2", "/api/b", "", "completed", "", pastTime);
        keys.AddIdempotencyKey(transaction, "user:3", "req_3", "/api/c", "", "completed", "", futureTime);

        int64_t totalCount = 0;
        KeyValueTableArray rows = keys.GetExpiredIdempotencyKeys(transaction, kTestCurrentTimeUs, 0, 0, &totalCount);
        EXPECT_EQ(rows.size(), 2u);
        EXPECT_EQ(totalCount, 2);
    });
}

// SetStatus
TEST(IdempotencyKeysTest, SetStatusBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetStatusBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_1", "/api/test",
            "", "pending", "", kTestExpiresUs);

        keys.SetStatus(transaction, id, "completed");

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysStatus)], "completed");
    });
}

// SetStatusAndResponse
TEST(IdempotencyKeysTest, SetStatusAndResponseBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetStatusAndResponseBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_1", "/api/test",
            "", "pending", "", kTestExpiresUs);

        keys.SetStatusAndResponse(transaction, id, "completed", R"({"result": "ok"})");

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysStatus)], "completed");
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysResponseJson)], R"({"result": "ok"})");
    });
}

TEST(IdempotencyKeysTest, SetStatusAndResponseClearResponse) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("SetStatusAndResponseClearResponse", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_1", "/api/test",
            "", "completed", R"({"old": "response"})", kTestExpiresUs);

        keys.SetStatusAndResponse(transaction, id, "failed", "");

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_EQ(row[std::string(DbSchema::kIdempotencyKeysStatus)], "failed");
        EXPECT_TRUE(row[std::string(DbSchema::kIdempotencyKeysResponseJson)].empty());
    });
}

// DeleteIdempotencyKey
TEST(IdempotencyKeysTest, DeleteIdempotencyKeyBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteIdempotencyKeyBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        IdempotencyKeys keys(databaseHelper);
        int64_t id = keys.AddIdempotencyKey(
            transaction, "user:123", "req_1", "/api/test",
            "", "pending", "", kTestExpiresUs);

        keys.DeleteIdempotencyKey(transaction, id);

        KeyValueTable row = keys.GetIdempotencyKey(transaction, id);
        EXPECT_TRUE(row.empty());
    });
}

// DeleteExpiredIdempotencyKeys
TEST(IdempotencyKeysTest, DeleteExpiredIdempotencyKeysBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredIdempotencyKeysBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        int64_t pastTime = kTestCurrentTimeUs - 1000000LL;  // Expired
        int64_t futureTime = kTestCurrentTimeUs + 1000000LL;  // Not expired

        IdempotencyKeys keys(databaseHelper);
        keys.AddIdempotencyKey(transaction, "user:1", "req_1", "/api/a", "", "completed", "", pastTime);
        keys.AddIdempotencyKey(transaction, "user:2", "req_2", "/api/b", "", "completed", "", pastTime);
        int64_t keptId = keys.AddIdempotencyKey(transaction, "user:3", "req_3", "/api/c", "", "completed", "", futureTime);

        int64_t deleted = keys.DeleteExpiredIdempotencyKeys(transaction, kTestCurrentTimeUs);
        EXPECT_EQ(deleted, 2);

        // Verify only the non-expired one remains
        int64_t totalCount = 0;
        KeyValueTableArray remaining = keys.GetIdempotencyKeysByStatus(transaction, "completed", 0, 0, &totalCount);
        EXPECT_EQ(remaining.size(), 1u);
        EXPECT_EQ(totalCount, 1);
        EXPECT_EQ(remaining[0][std::string(DbSchema::kIdempotencyKeysId)], std::to_string(keptId));
    });
}

TEST(IdempotencyKeysTest, DeleteExpiredIdempotencyKeysNoExpiredRowsReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredIdempotencyKeysNoExpiredRowsReturnsZero",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        IdempotencyKeys keys(databaseHelper);

        int64_t futureTime = kTestCurrentTimeUs + 1000000LL;
        keys.AddIdempotencyKey(transaction, "user:1", "req_a", "/api/x", "", "completed", "", futureTime);

        int64_t deleted = keys.DeleteExpiredIdempotencyKeys(transaction, kTestCurrentTimeUs);
        EXPECT_EQ(deleted, 0);
    });
}

TEST(IdempotencyKeysTest, DeleteExpiredIdempotencyKeysBoundaryNotInclusive) {
    // Rows where expires_us == currentTimeUs should NOT be deleted (strictly less than).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteExpiredIdempotencyKeysBoundaryNotInclusive",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        IdempotencyKeys keys(databaseHelper);

        keys.AddIdempotencyKey(transaction, "user:1", "req_b", "/api/y", "", "completed", "", 1000);

        int64_t deleted = keys.DeleteExpiredIdempotencyKeys(transaction, 1000);
        EXPECT_EQ(deleted, 0);

        deleted = keys.DeleteExpiredIdempotencyKeys(transaction, 1001);
        EXPECT_EQ(deleted, 1);
    });
}

} // namespace
} // namespace TableHelpers
