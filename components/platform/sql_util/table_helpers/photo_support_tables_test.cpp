#include "photo_support_tables.h"

#include <gtest/gtest.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

TEST(PhotoSupportTablesTest, AddAndGetAll) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetAll", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables helper(databaseHelper);
        helper.AddPhotoSupportTable(transaction, "people");
        helper.AddPhotoSupportTable(transaction, "home_page_photos");

        KeyValueTableArray all = helper.GetAllPhotoSupportTables(transaction);
        EXPECT_EQ(all.size(), 2u);
    });
}

TEST(PhotoSupportTablesTest, IsPhotoSupportedForTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsPhotoSupported", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables helper(databaseHelper);
        EXPECT_FALSE(helper.IsPhotoSupportedForTable(transaction, "people"));

        helper.AddPhotoSupportTable(transaction, "people");
        EXPECT_TRUE(helper.IsPhotoSupportedForTable(transaction, "people"));
        EXPECT_FALSE(helper.IsPhotoSupportedForTable(transaction, "classes"));
    });
}

TEST(PhotoSupportTablesTest, DeletePhotoSupportTable) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeletePhotoSupportTable", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        PhotoSupportTables helper(databaseHelper);
        helper.AddPhotoSupportTable(transaction, "people");
        helper.AddPhotoSupportTable(transaction, "home_page_photos");

        helper.DeletePhotoSupportTable(transaction, "people");
        EXPECT_FALSE(helper.IsPhotoSupportedForTable(transaction, "people"));
        EXPECT_TRUE(helper.IsPhotoSupportedForTable(transaction, "home_page_photos"));
    });
}

}  // namespace
}  // namespace TableHelpers
