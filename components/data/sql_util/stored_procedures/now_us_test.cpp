#include "sql_util/stored_procedures/now_us.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdint>
#include <cstdlib>

#include "test/src/util/database_test_helper.h"
#include "sql_util/database_access/database_util.h"

namespace StoredProcedures {
namespace {

TEST(CreateNowUsTest, CreateNowUsBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("CreateGetAdminAlertsInWindowBasic", [&](Transaction& transaction) {
        // Install the function
        CreateNowUs(transaction);

        // Call the function twice
        std::string val1Str = transaction.RunSqlStatementReturningOneValue("SELECT now_us()");
        std::string val2Str = transaction.RunSqlStatementReturningOneValue("SELECT now_us()");

        // Parse as microseconds since epoch
        int64_t v1 = std::stoll(val1Str);
        int64_t v2 = std::stoll(val2Str);

        // Both should be positive
        ASSERT_GT(v1, 0);
        ASSERT_GT(v2, 0);

        // And within one hour of each other
        constexpr int64_t kHourUs = 60LL * 60LL * 1000000LL;
        int64_t diff = (v2 >= v1) ? (v2 - v1) : (v1 - v2);
        ASSERT_LT(diff, kHourUs);
        });
}

} // namespace
} // namespace StoredProcedures