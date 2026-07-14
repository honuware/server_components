#include "sql_util/table_helpers/admin_table_display_template.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "db_schema/admin_table_display_template.h"

namespace TableHelpers {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::IsEmpty;

TEST(AdminTableDisplayTemplateTest, SetAndGetDisplayTemplate)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SetAndGetDisplayTemplate", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTableDisplayTemplate helper(databaseHelper);
        helper.SetDisplayTemplate(transaction, "people", "{first_name} {last_name}");

        EXPECT_EQ(helper.GetDisplayTemplate(transaction, "people"), "{first_name} {last_name}");
    });
}

TEST(AdminTableDisplayTemplateTest, GetDisplayTemplateReturnsEmptyForMissingTable)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetDisplayTemplateReturnsEmptyForMissingTable", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTableDisplayTemplate helper(databaseHelper);

        EXPECT_EQ(helper.GetDisplayTemplate(transaction, "nonexistent"), "");
    });
}

TEST(AdminTableDisplayTemplateTest, GetAllDisplayTemplates)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetAllDisplayTemplates", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTableDisplayTemplate helper(databaseHelper);
        helper.SetDisplayTemplate(transaction, "people", "{first_name} {last_name}");
        helper.SetDisplayTemplate(transaction, "roles", "{name}");
        helper.SetDisplayTemplate(transaction, "products", "{name}");

        KeyValueTable all = helper.GetAllDisplayTemplates(transaction);
        EXPECT_THAT(all, UnorderedElementsAre(
            Pair("people", "{first_name} {last_name}"),
            Pair("roles", "{name}"),
            Pair("products", "{name}")));
    });
}

TEST(AdminTableDisplayTemplateTest, GetAllDisplayTemplatesReturnsEmptyWhenNoneSet)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetAllDisplayTemplatesReturnsEmptyWhenNoneSet", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTableDisplayTemplate helper(databaseHelper);

        EXPECT_THAT(helper.GetAllDisplayTemplates(transaction), IsEmpty());
    });
}

TEST(AdminTableDisplayTemplateTest, DeleteDisplayTemplate)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeleteDisplayTemplate", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        AdminTableDisplayTemplate helper(databaseHelper);
        helper.SetDisplayTemplate(transaction, "people", "{first_name} {last_name}");
        helper.SetDisplayTemplate(transaction, "roles", "{name}");

        EXPECT_EQ(helper.GetDisplayTemplate(transaction, "people"), "{first_name} {last_name}");

        helper.DeleteDisplayTemplate(transaction, "people");

        EXPECT_EQ(helper.GetDisplayTemplate(transaction, "people"), "");
        EXPECT_EQ(helper.GetDisplayTemplate(transaction, "roles"), "{name}");
    });
}

} // namespace
} // namespace TableHelpers
