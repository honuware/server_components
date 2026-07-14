#include "sql_util/stored_procedures/get_admin_alerts_in_window.h"
#include "sql_util/stored_procedures/now_us.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdint>
#include <string>

#include "db_schema/admin_alerts.h"
#include "sql_util/table_helpers/admin_alerts.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"
#include "util/types.h"

namespace StoredProcedures {
namespace {

using ::testing::ElementsAre;

constexpr int64_t kTenMinutesUs = 10LL * 60LL * 1000000LL;
constexpr int64_t kWeekUs       = 7LL * 24LL * 60LL * 1000000LL;

TEST(CreateGetAdminAlertsInWindowTest, CreateGetAdminAlertsInWindowBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("CreateGetAdminAlertsInWindowBasic", [&](Transaction& transaction) {
        // Ensure helper function and table exist, then install the SP
        CreateNowUs(transaction);

        CreateGetAdminAlertsInWindow(transaction);

        TableHelpers::AdminAlerts alerts(testDatabaseUtil.GetDatabaseHelper());
        alerts.AddAdminAlert(transaction, "first alert");
        alerts.AddAdminAlert(transaction, "second alert");

        KeyValueTableArray keyValueTableArray = alerts.GetAdminAlerts(transaction, kTenMinutesUs);
        ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
            keyValueTableArray,
            {
                {
                    { std::string(DbSchema::kAdminAlertsDescription), "second alert" }
                },
                {
                    { std::string(DbSchema::kAdminAlertsDescription), "first alert" }
                }
            },
            std::string(DbSchema::kAdminAlertsId), std::string(DbSchema::kAdminAlertsCreatedAt)));
        });
}

TEST(CreateGetAdminAlertsInWindowTest, CreateGetAdminAlertsInWindowOutOfRange) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("CreateGetAdminAlertsInWindowBasic", [&](Transaction& transaction) {
        // Ensure helper function and table exist, then install the SP
        CreateNowUs(transaction);

        CreateGetAdminAlertsInWindow(transaction);

        TableHelpers::AdminAlerts alerts(testDatabaseUtil.GetDatabaseHelper());
        alerts.AddAdminAlert(transaction, "in range");
        alerts.AddAdminAlert(transaction, "to move back a week");

        // Fetch both to get their ids
        KeyValueTableArray before = alerts.GetAdminAlerts(transaction, kTenMinutesUs);
        ASSERT_EQ(before.size(), 2u);

        // Find the id of the "to move back a week" alert
        std::string idToMove;
        for(const auto& row : before) {
            if(row.at(std::string(DbSchema::kAdminAlertsDescription)) == "to move back a week") {
                // Found it
                idToMove = row.at(std::string(DbSchema::kAdminAlertsId));
                break;
            }
        }
        ASSERT_FALSE(idToMove.empty());

        // Compute a timestamp a week in the past
        int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        const std::string pastUs = std::to_string(nowUs - kWeekUs);

        // Update created_at for that row into the past via DbCrud::UpdateRow.
        KeyValueTable kv = { { std::string(DbSchema::kAdminAlertsCreatedAt), pastUs } };
        DbCrud::UpdateRow(
            transaction,
            testDatabaseUtil.GetDatabaseHelper(),
            DbSchema::kAdminAlertsTable,
            DbSchema::kAdminAlertsId,
            idToMove,
            kv);

        // Query the last 10 minutes again; only the "in range" entry should remain
        KeyValueTableArray after = alerts.GetAdminAlerts(transaction, kTenMinutesUs);
        ASSERT_EQ(after.size(), 1u);
        EXPECT_EQ(
            after[0].at(std::string(DbSchema::kAdminAlertsDescription)),
            "in range");
        });
}

} // namespace
} // namespace StoredProcedures