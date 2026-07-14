#include "sessions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/sessions.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_util.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

static int AddTestPerson(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view email) {
    People people(testDb.GetDatabaseHelper());
    return people.AddPerson(transaction, email, "First", "Last", "hash");
}

TEST(SessionsTest, AddSessionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddSessionBasic", [&](Transaction& transaction) {


        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "user@example.com");

        int64_t oneHourUs = 60LL * 60LL * 1000000LL;
        int sessionId = sessions.AddSession(transaction, personId, oneHourUs);
        ASSERT_GT(sessionId, 0);

        // Validate via DB fetch
        KeyValueTable row = sessions.LookupSessionById(transaction, sessionId);
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsPersonId)), StringFromInt(personId));
        // expires_at should be >= now
        int64_t expiresAt = std::stoll(row.at(std::string(DbSchema::kSessionsExpiresAt)));
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue(
            "SELECT now_us()"));
        EXPECT_GE(expiresAt, nowUs);
    });
}

TEST(SessionsTest, AddSessionUserNotPresent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddSessionUserNotPresent", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());

        // FK should fail; expect exception
        int64_t oneHourUs = 60LL * 60LL * 1000000LL;
        EXPECT_THROW((void)sessions.AddSession(transaction, 424242, oneHourUs), std::exception);
    });
}

TEST(SessionsTest, LookupSessionByIdBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByIdBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "luid@example.com");

        int sessionId = sessions.AddSession(transaction, personId, 1000000);
        KeyValueTable row = sessions.LookupSessionById(transaction, sessionId);
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsId)), StringFromInt(sessionId));
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsPersonId)), StringFromInt(personId));
    });
}

TEST(SessionsTest, LookupSessionByIdNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByIdNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)sessions.LookupSessionById(transaction, 999999), std::runtime_error);
    });
}

TEST(SessionsTest, LookupSessionByUuidBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByIdBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "luid@example.com");

        int sessionId = sessions.AddSession(transaction, personId, 1000000);
        KeyValueTable rowLookup = DbCrud::LookupRowByValue(
            transaction, testDb.GetDatabaseHelper(), DbSchema::kSessionsTable,
            DbSchema::kSessionsId, StringFromInt(sessionId));
            
        KeyValueTable row = sessions.LookupSessionByUuid(
            transaction, rowLookup[std::string(DbSchema::kSessionsUuid)]);
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsId)), StringFromInt(sessionId));
        EXPECT_EQ(row.at(std::string(DbSchema::kSessionsPersonId)), StringFromInt(personId));
        });
}

TEST(SessionsTest, LookupSessionByUuidNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByIdNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)sessions.LookupSessionByUuid(transaction, "not found"), std::runtime_error);
        });
}


TEST(SessionsTest, LookupSessionByPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByPersonBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "lup@example.com");

        int id1 = sessions.AddSession(transaction, personId, 1000000);
        int id2 = sessions.AddSession(transaction, personId, 2000000);
        (void)id1; (void)id2;

        KeyValueTableArray rows = sessions.LookupSessionByPerson(transaction, personId);
        ASSERT_GE(rows.size(), 2u);
        // Verify that all rows correspond to our person
        for (const auto& kv : rows) {
            EXPECT_EQ(kv.at(std::string(DbSchema::kSessionsPersonId)), StringFromInt(personId));
        }
    });
}

TEST(SessionsTest, LookupSessionByPersonNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("LookupSessionByPersonNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        EXPECT_THROW((void)sessions.LookupSessionByPerson(transaction, 777777), std::runtime_error);
    });
}

TEST(SessionsTest, UpdateLastSeenBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateLastSeenBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "uls@example.com");
        int sessionId = sessions.AddSession(transaction, personId, 1000000);

        KeyValueTable before = sessions.LookupSessionById(transaction, sessionId);
        std::string beforeSeen = before.at(std::string(DbSchema::kSessionsLastSeenAt));

        sessions.UpdateLastSeen(transaction, sessionId);

        KeyValueTable after = sessions.LookupSessionById(transaction, sessionId);
        std::string afterSeen = after.at(std::string(DbSchema::kSessionsLastSeenAt));

        // It should change (or at minimum be set; practically should differ)
        EXPECT_NE(beforeSeen, afterSeen);
    });
}

TEST(SessionsTest, UpdateLastSeenNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateLastSeenNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        EXPECT_THROW(sessions.UpdateLastSeen(transaction, 565656), std::runtime_error);
    });
}

TEST(SessionsTest, RemoveSessionsForUserBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveSessionsForUserBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rsfu@example.com");

        sessions.AddSession(transaction, personId, 1000000);
        sessions.AddSession(transaction, personId, 1000000);

        // Remove them
        sessions.RemoveSessionsForUser(transaction, personId);

        // Verify via DB count
        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM sessions WHERE person_id = $1", StringFromInt(personId));
        EXPECT_EQ(countStr, "0");

        // Lookup should throw now
        EXPECT_THROW((void)sessions.LookupSessionByPerson(transaction, personId), std::runtime_error);
    });
}

TEST(SessionsTest, RemoveSessionsForUserNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveSessionsForUserNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        // Should not throw for missing user
        sessions.RemoveSessionsForUser(transaction, 123456);
        // Nothing else to assert; just ensure it returns normally
    });
}

TEST(SessionsTest, RemoveSessionBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveSessionBasic", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        int personId = AddTestPerson(transaction, testDb, "rsb@example.com");
        int sessionId = sessions.AddSession(transaction, personId, 1000000);

        sessions.RemoveSession(transaction, sessionId);

        // Validate removal
        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM sessions WHERE id = $1", StringFromInt(sessionId));
        EXPECT_EQ(countStr, "0");

        EXPECT_THROW((void)sessions.LookupSessionById(transaction, sessionId), std::runtime_error);
    });
}

TEST(SessionsTest, RemoveSessionNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("RemoveSessionNotFound", [&](Transaction& transaction) {

        Sessions sessions(testDb.GetDatabaseHelper());
        // Should not throw on missing
        sessions.RemoveSession(transaction, 999999);
    });
}

} // namespace
} // namespace TableHelpers