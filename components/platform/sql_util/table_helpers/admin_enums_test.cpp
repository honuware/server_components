#include "sql_util/table_helpers/admin_enums.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_enums.h"

namespace TableHelpers {
namespace {
    using ::testing::UnorderedElementsAre;
    using ::testing::ContainerEq;

    TEST(AdminEnumsTest, AddAdminEnumBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("AddAdminEnumBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);

            int64_t id1 = adminEnums.AddAdminEnum(transaction, "currency");
            int64_t id2 = adminEnums.AddAdminEnum(transaction, "payment_status");

            EXPECT_NE(id1, 0);
            EXPECT_NE(id2, 0);
            EXPECT_NE(id1, id2);

            KeyValueTableArray all = adminEnums.GetAdminEnums(transaction);
            EXPECT_EQ(all.size(), 2);
        });
    }

    TEST(AdminEnumsTest, GetAdminEnumByName)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminEnumByName", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);

            adminEnums.AddAdminEnum(transaction, "currency");

            KeyValueTable result;
            ASSERT_TRUE(adminEnums.GetAdminEnum(transaction, "currency", result));
            EXPECT_EQ(result.at(std::string(DbSchema::kAdminEnumsName)), "currency");
        });
    }

    TEST(AdminEnumsTest, GetAdminEnumByNameNotFound)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminEnumByNameNotFound", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);

            KeyValueTable result;
            ASSERT_FALSE(adminEnums.GetAdminEnum(transaction, "nonexistent", result));
        });
    }

    TEST(AdminEnumsTest, DeleteAdminEnumBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("DeleteAdminEnumBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);

            int64_t id1 = adminEnums.AddAdminEnum(transaction, "currency");
            int64_t id2 = adminEnums.AddAdminEnum(transaction, "payment_status");
            EXPECT_EQ(adminEnums.GetAdminEnums(transaction).size(), 2);

            adminEnums.DeleteAdminEnum(transaction, id1);
            EXPECT_EQ(adminEnums.GetAdminEnums(transaction).size(), 1);

            KeyValueTable result;
            ASSERT_FALSE(adminEnums.GetAdminEnum(transaction, "currency", result));
            ASSERT_TRUE(adminEnums.GetAdminEnum(transaction, "payment_status", result));

            adminEnums.DeleteAdminEnum(transaction, id2);
            EXPECT_EQ(adminEnums.GetAdminEnums(transaction).size(), 0);
        });
    }

} // namespace
}  // namespace TableHelpers
