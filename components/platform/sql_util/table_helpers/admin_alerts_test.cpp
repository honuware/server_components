#include "sql_util/table_helpers/admin_alerts.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/admin_alerts.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

using ::testing::ElementsAre;

constexpr int64_t kTenMinutesUs = 10LL * 60LL * 1000000LL;

TEST(AdminAlertsTest, AddAdminAlertBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAdminAlertBasic", [&](Transaction& transaction) {
        auto db = testDb.GetDatabaseHelper();

        AdminAlerts alerts(db);
        alerts.AddAdminAlert(transaction, "first alert");
        alerts.AddAdminAlert(transaction, "second alert");

        // Verify two rows exist with expected descriptions
        KeyValueTableArray keyValueTableArray = DbCrud::GetTableRows(transaction, db, DbSchema::kAdminAlertsTable);
        ASSERT_EQ(keyValueTableArray.size(), 2u);

        StringSet descs;
        descs.insert(keyValueTableArray[0].at(std::string(DbSchema::kAdminAlertsDescription)));
        descs.insert(keyValueTableArray[1].at(std::string(DbSchema::kAdminAlertsDescription)));
        EXPECT_TRUE(SetContains(descs, "first alert"));
        EXPECT_TRUE(SetContains(descs, "second alert"));
        });
}

TEST(AdminAlertsTest, GetAdminAlertsBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetAdminAlertsBasic", [&](Transaction& transaction) {
        auto db = testDb.GetDatabaseHelper();

        AdminAlerts alerts(db);
        alerts.AddAdminAlert(transaction, "first alert");
        alerts.AddAdminAlert(transaction, "second alert");

        KeyValueTableArray keyValueTableArray = alerts.GetAdminAlerts(transaction, kTenMinutesUs);
        ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
            keyValueTableArray,
            {
                {
                    {std::string(DbSchema::kAdminAlertsDescription), "first alert"},
                },
                {
                    {std::string(DbSchema::kAdminAlertsDescription), "second alert"},
                }
            },
            std::string(DbSchema::kAdminAlertsId), std::string(DbSchema::kAdminAlertsCreatedAt)));
        });
}

// Phase 9.2 of the security review: digest-pull. The digest job
// looks up `last_notified_us`, asks for rows newer than that, and
// stamps `last_notified_us = max(created_at)` after sending. The
// strict `>` boundary is the contract that lets the stamp-back work
// without double-sending: a row at exactly `last_notified_us`
// already went out on the previous tick.
TEST(AdminAlertsTest, GetAdminAlertsSinceFiltersAndOrders) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetAdminAlertsSinceFiltersAndOrders",
        [&](Transaction& transaction) {
        auto db = testDb.GetDatabaseHelper();
        AdminAlerts alerts(db);

        // Direct-insert with hand-rolled created_at values so the
        // ordering+filter contract is exercised on known data
        // (the auto-default would assign a clock-derived value).
        transaction.RunSqlStatement(
            "INSERT INTO admin_alerts (created_at, description) "
            "VALUES ($1, $2)",
            std::string("100"), std::string("very old"));
        transaction.RunSqlStatement(
            "INSERT INTO admin_alerts (created_at, description) "
            "VALUES ($1, $2)",
            std::string("200"), std::string("at boundary"));
        transaction.RunSqlStatement(
            "INSERT INTO admin_alerts (created_at, description) "
            "VALUES ($1, $2)",
            std::string("300"), std::string("fresh-1"));
        transaction.RunSqlStatement(
            "INSERT INTO admin_alerts (created_at, description) "
            "VALUES ($1, $2)",
            std::string("400"), std::string("fresh-2"));

        // since = 200 → only the rows STRICTLY greater than 200.
        auto rows = alerts.GetAdminAlertsSince(transaction, 200);
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].at(std::string(DbSchema::kAdminAlertsDescription)),
                  "fresh-1");
        EXPECT_EQ(rows[1].at(std::string(DbSchema::kAdminAlertsDescription)),
                  "fresh-2");

        // since = 0 → everything.
        EXPECT_EQ(alerts.GetAdminAlertsSince(transaction, 0).size(), 4u);

        // since past the max → empty.
        EXPECT_TRUE(
            alerts.GetAdminAlertsSince(transaction, 9999).empty());
    });
}

TEST(AdminAlertsTest, GetAdminAlertsSinceNegativeTreatedAsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetAdminAlertsSinceNegativeTreatedAsZero",
        [&](Transaction& transaction) {
        auto db = testDb.GetDatabaseHelper();
        AdminAlerts alerts(db);
        transaction.RunSqlStatement(
            "INSERT INTO admin_alerts (created_at, description) "
            "VALUES ($1, $2)",
            std::string("1"), std::string("only"));
        // Negative is sanitized — should return everything, not error.
        EXPECT_EQ(alerts.GetAdminAlertsSince(transaction, -1).size(), 1u);
    });
}

TEST(AdminAlertsTest, DeleteAdminAlertBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteAdminAlertBasic", [&](Transaction& transaction) {
        auto db = testDb.GetDatabaseHelper();

        AdminAlerts alerts(db);
        alerts.AddAdminAlert(transaction, "keep me");
        alerts.AddAdminAlert(transaction, "delete me");

        // Lookup ids
        KeyValueTableArray keyValueTableArray = DbCrud::GetTableRows(transaction, db, DbSchema::kAdminAlertsTable);
        ASSERT_EQ(keyValueTableArray.size(), 2u);

        int64_t idToDelete = -1;
        for (size_t i = 0; i < keyValueTableArray.size(); ++i) {
            if (keyValueTableArray[i].at(std::string(DbSchema::kAdminAlertsDescription)) == "delete me") {
                idToDelete = std::stoll(keyValueTableArray[i].at(std::string(DbSchema::kAdminAlertsId)));
                break;
            }
        }
        ASSERT_GT(idToDelete, 0);

        alerts.DeleteAdminAlert(transaction, idToDelete);

        // Verify only "keep me" remains
        keyValueTableArray = DbCrud::GetTableRows(transaction, db, DbSchema::kAdminAlertsTable);
        ASSERT_EQ(keyValueTableArray.size(), 1u);
        EXPECT_EQ(keyValueTableArray[0].at(std::string(DbSchema::kAdminAlertsDescription)), "keep me");
        });
}

} // namespace
} // namespace TableHelpers