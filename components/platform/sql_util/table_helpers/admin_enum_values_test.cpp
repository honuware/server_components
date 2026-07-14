#include "sql_util/table_helpers/admin_enum_values.h"
#include "sql_util/table_helpers/admin_enums.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "db_schema/admin_enums.h"
#include "db_schema/admin_enum_values.h"

namespace TableHelpers {
namespace {
    using ::testing::UnorderedElementsAre;
    using ::testing::ContainerEq;

    TEST(AdminEnumValuesTest, AddAdminEnumValueBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("AddAdminEnumValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);
            AdminEnumValues adminEnumValues(databaseHelper);

            int64_t enumId = adminEnums.AddAdminEnum(transaction, "payment_status");

            adminEnumValues.AddAdminEnumValue(transaction, enumId, "COMPLETED", 0);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "PENDING", 1);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "FAILED", 2);

            KeyValueTableArray values = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(values.size(), 3);
            EXPECT_EQ(values[0].at(std::string(DbSchema::kAdminEnumValuesName)), "COMPLETED");
            EXPECT_EQ(values[1].at(std::string(DbSchema::kAdminEnumValuesName)), "PENDING");
            EXPECT_EQ(values[2].at(std::string(DbSchema::kAdminEnumValuesName)), "FAILED");
        });
    }

    TEST(AdminEnumValuesTest, GetAdminEnumValuesOrderedByValue)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminEnumValuesOrderedByValue", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);
            AdminEnumValues adminEnumValues(databaseHelper);

            int64_t enumId = adminEnums.AddAdminEnum(transaction, "purchase_status");

            // Insert out of order
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "cancelled", 3);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "pending", 0);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "funded", 1);

            KeyValueTableArray values = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(values.size(), 3);
            EXPECT_EQ(values[0].at(std::string(DbSchema::kAdminEnumValuesName)), "pending");
            EXPECT_EQ(values[1].at(std::string(DbSchema::kAdminEnumValuesName)), "funded");
            EXPECT_EQ(values[2].at(std::string(DbSchema::kAdminEnumValuesName)), "cancelled");
        });
    }

    TEST(AdminEnumValuesTest, GetAdminEnumValuesForDifferentEnums)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("GetAdminEnumValuesForDifferentEnums", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);
            AdminEnumValues adminEnumValues(databaseHelper);

            int64_t currencyId = adminEnums.AddAdminEnum(transaction, "currency");
            int64_t providerIdEnum = adminEnums.AddAdminEnum(transaction, "payment_provider");

            adminEnumValues.AddAdminEnumValue(transaction, currencyId, "USD", 0);
            adminEnumValues.AddAdminEnumValue(transaction, providerIdEnum, "square", 0);
            adminEnumValues.AddAdminEnumValue(transaction, providerIdEnum, "cash", 1);

            KeyValueTableArray currencyValues = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, currencyId);
            ASSERT_EQ(currencyValues.size(), 1);
            EXPECT_EQ(currencyValues[0].at(std::string(DbSchema::kAdminEnumValuesName)), "USD");

            KeyValueTableArray providerValues = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, providerIdEnum);
            ASSERT_EQ(providerValues.size(), 2);
            EXPECT_EQ(providerValues[0].at(std::string(DbSchema::kAdminEnumValuesName)), "square");
            EXPECT_EQ(providerValues[1].at(std::string(DbSchema::kAdminEnumValuesName)), "cash");
        });
    }

    TEST(AdminEnumValuesTest, UpdateAdminEnumValueBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("UpdateAdminEnumValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);
            AdminEnumValues adminEnumValues(databaseHelper);

            int64_t enumId = adminEnums.AddAdminEnum(transaction, "payment_status");
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "COMPLETED", 0);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "PENDING", 1);

            KeyValueTableArray before = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(before.size(), 2);
            EXPECT_EQ(before[0].at(std::string(DbSchema::kAdminEnumValuesName)), "COMPLETED");

            // Update first value: rename and change ordering
            int64_t firstId = std::stoll(before[0].at(std::string(DbSchema::kAdminEnumValuesId)));
            adminEnumValues.UpdateAdminEnumValue(transaction, firstId, "SUCCEEDED", 5);

            KeyValueTableArray after = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(after.size(), 2);
            // PENDING (value=1) should now come first, SUCCEEDED (value=5) second
            EXPECT_EQ(after[0].at(std::string(DbSchema::kAdminEnumValuesName)), "PENDING");
            EXPECT_EQ(after[1].at(std::string(DbSchema::kAdminEnumValuesName)), "SUCCEEDED");
            EXPECT_EQ(after[1].at(std::string(DbSchema::kAdminEnumValuesValue)), "5");
        });
    }

    TEST(AdminEnumValuesTest, DeleteAdminEnumValueBasic)
    {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("DeleteAdminEnumValueBasic", [&](Transaction& transaction) {

            DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
            AdminEnums adminEnums(databaseHelper);
            AdminEnumValues adminEnumValues(databaseHelper);

            int64_t enumId = adminEnums.AddAdminEnum(transaction, "payment_provider");
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "square", 0);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "cash", 1);
            adminEnumValues.AddAdminEnumValue(transaction, enumId, "comp", 2);

            KeyValueTableArray before = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(before.size(), 3);

            // Delete the middle value ("cash", id=2)
            int64_t cashId = std::stoll(before[1].at(std::string(DbSchema::kAdminEnumValuesId)));
            adminEnumValues.DeleteAdminEnumValue(transaction, cashId);

            KeyValueTableArray after = adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
            ASSERT_EQ(after.size(), 2);
            EXPECT_EQ(after[0].at(std::string(DbSchema::kAdminEnumValuesName)), "square");
            EXPECT_EQ(after[1].at(std::string(DbSchema::kAdminEnumValuesName)), "comp");
        });
    }

} // namespace
}  // namespace TableHelpers
