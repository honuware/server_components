#include "sql_util/database_access/database_metadata.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbMeta {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::ElementsAre;
        using ::testing::Eq;
        using ::testing::Not;

        void MakePeopleTable(Transaction& transaction, TestDatabaseUtil& testDatabaseUtil) {
            auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            constexpr std::string_view kPeople = "test_people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "first_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "last_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "password_hash", DB_TYPE_STRING);
            DbOps::CreateTable(transaction, databaseInfo, kPeople);
        }

        void MakeOrdersTable(Transaction& transaction, TestDatabaseUtil& testDatabaseUtil) {
            auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            constexpr std::string_view kPeople = "test_people";
            constexpr std::string_view kOrders = "test_orders";
            databaseInfo.AddTable(kOrders);
            databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnForeignKeyRef(kPeople, "person_id", kOrders, "parent_person_id");
            databaseInfo.AddColumnSimple(kOrders, "item", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kOrders, "amount", DB_TYPE_INT);
            DbOps::CreateTable(transaction, databaseInfo, kOrders);
        }

        TEST(DatabaseMetadataTest, ListDatabasesBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            // Assert the ACTIVE test database (injected via GlobalDatabaseTestSupport)
            // is listed, not a hardcoded brand name: honuware's suite runs against
            // "honuware_test", knottyyoga's against "test_knottyyoga".
            const std::string activeDatabase(
                testDatabaseUtil.GetDatabaseInfo().GetDatabaseName());
            testDatabaseUtil.RunInTransaction("ListDatabasesBasic", [&](Transaction& transaction) {
                EXPECT_THAT(ListDatabases(transaction), Contains(activeDatabase));
                });
        }

        TEST(DatabaseMetadataTest, ListTablesBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListTablesBasic", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                EXPECT_THAT(ListTables(transaction), Contains("test_people"));
                EXPECT_THAT(ListTables(transaction), Contains("test_orders"));
                });
        }

        TEST(DatabaseMetadataTest, ListViewsBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListViewsBasic", [&](Transaction& transaction) {
                EXPECT_THAT(ListViews(transaction), Contains("columns"));
                });
        }

        TEST(DatabaseMetadataTest, DatabaseTypesFromDatabaseColumnInfoBasic)
        {
            DatabaseColumnInfo info = { "person_id", 1, "integer", 32, "int4", true, false };

            std::vector<std::pair<std::string, DatabaseTypes>> table = {
                {"BOOL", DB_TYPE_BOOL},
                { "BOOLEAN", DB_TYPE_BOOL },
                { "BYTEA", DB_TYPE_BYTES },
                { "CHARACTER VARYING", DB_TYPE_STRING },
                { "VARCHAR", DB_TYPE_STRING },
                { "DATE", DB_TYPE_DATE },
                { "DOUBLE PRECISION", DB_TYPE_DOUBLE },
                { "FLOAT8", DB_TYPE_DOUBLE },
                { "INTEGER", DB_TYPE_INT },
                { "INT", DB_TYPE_INT },
                { "INT4", DB_TYPE_INT },
                { "JSON", DB_TYPE_JSON },
                { "SERIAL", DB_TYPE_SERIAL },
                { "SERIAL4", DB_TYPE_SERIAL },
                { "TEXT", DB_TYPE_STRING },
                { "TIME WITHOUT TIME ZONE", DB_TYPE_TIME },
                { "TIME WITH TIME ZONE", DB_TYPE_TIME },
                { "TIMTZ", DB_TYPE_TIME },
                { "UUID", DB_TYPE_UUID }
            };
            for (const auto& [typeString, type]
                : table) {
                info.dataType = typeString;
                ASSERT_EQ(DatabaseTypesFromDatabaseColumnInfo(info), type);
            }
        }

        TEST(DatabaseMetadataTest, DatabaseTypesFromDatabaseColumnInfoInvalid)
        {
            DatabaseColumnInfo info = { "person_id", 1, "invalid", 32, "int4", true, false };

            try {
                DatabaseTypesFromDatabaseColumnInfo(info);
                ASSERT_TRUE(false); // We should never get here
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseTypesFromDatabaseColumnInfo - "
                    "invalid database type: invalid");
            }
        }

        TEST(DatabaseMetadataTest, ListColumnsBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListColumnsBasic", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);

                DatabaseColumnInfoArray databaseColumnInfoArray = {
                    {"person_id", 1, "serial", 32, "int4", true, false},
                    {"email", 2, "character varying", 0, "varchar", false, true},
                    {"first_name", 3, "character varying", 0, "varchar", false, false},
                    {"last_name", 4, "character varying", 0, "varchar", false, false},
                    {"password_hash", 5, "character varying", 0, "varchar", false, false} };
                EXPECT_THAT(
                    ListColumns(transaction, "test_people"),
                    ElementsAreArray(
                        databaseColumnInfoArray.begin(),
                        databaseColumnInfoArray.end()));
                });
        }

        TEST(DatabaseMetadataTest, GetPrimaryKeyBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetPrimaryKeyBasic", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);

                EXPECT_EQ(GetPrimaryKey(transaction, "test_people"), "person_id");
                });
        }

        TEST(DatabaseMetadataTest, ListChildTablesBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListChildTablesBasic", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                EXPECT_THAT(
                    ListChildTables(transaction, "test_people"),
                    UnorderedElementsAre("test_orders"));
                });
        }

        TEST(DatabaseMetadataTest, ListParentTablesBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListParentTablesBasic", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                EXPECT_THAT(
                    ListParentTables(transaction, "test_orders"),
                    UnorderedElementsAre("test_people"));
                });
        }

        TEST(DatabaseMetadataTest, ListRootTablesBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListRootTablesBasic", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                info.AddTable("table2");
                info.AddColumnForeignKeyRef("table1", "column1", "table2", "column2");
                DbOps::CreateTable(transaction, info, "table1");
                DbOps::CreateTable(transaction, info, "table2");
                EXPECT_THAT(ListRootTables(transaction), Contains("table1"));
                EXPECT_THAT(ListRootTables(transaction), Not(Contains("table2")));
                });
        }

        TEST(DatabaseMetadataTest, GetParentChildForeignKeyColumns)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetParentChildForeignKeyColumns", [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                std::string parentColumnName;
                std::string childColumnName;
                GetParentChildForeignKeyColumns(
                    transaction, "test_people", "test_orders", parentColumnName, childColumnName);
                ASSERT_EQ(parentColumnName, "person_id");
                ASSERT_EQ(childColumnName, "parent_person_id");
                });
        }

        TEST(DatabaseMetadataTest, StringArrayFromDatabaseColumnInfoArrayBasic)
        {
            DatabaseColumnInfoArray databaseColumnInfoArray = {
                {"column1", 0, "mytype", 0, "mytype", false},
                {"column2", 1, "mytype", 0, "mytype", false}
            };
            EXPECT_THAT(
                StringArrayFromDatabaseColumnInfoArray(databaseColumnInfoArray),
                UnorderedElementsAre("column1", "column2"));
        }

        TEST(DatabaseMetadataTest, DatabaseInfoFromDatabaseBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetParentChildForeignKeyColumns", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo databaseInfo("knotty_yoga_test");

                constexpr std::string_view kPeople = "test_people";
                databaseInfo.AddTable(kPeople);
                databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
                databaseInfo.AddColumnSimple(kPeople, "first_name", DB_TYPE_STRING);
                DbOps::CreateTable(transaction, databaseInfo, kPeople);

                constexpr std::string_view kOrders = "test_orders";
                databaseInfo.AddTable(kOrders);
                databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnForeignKeyRef(
                    kPeople, "person_id", kOrders, "parent_person_id");
                databaseInfo.AddColumnSimple(kOrders, "item", DB_TYPE_STRING);
                DbOps::CreateTable(transaction, databaseInfo, kOrders);

                DbSchema::DatabaseInfo databaseInfoTest = DatabaseInfoFromDatabase(
                    transaction, "knotty_yoga_test", { "test_people", "test_orders" });

                ASSERT_EQ(
                    DbOps::GenerateCreateTableSql(databaseInfo, "test_orders"),
                    DbOps::GenerateCreateTableSql(databaseInfoTest, "test_orders"));
                ASSERT_EQ(
                    DbOps::GenerateCreateTableSql(databaseInfo, "test_people"),
                    DbOps::GenerateCreateTableSql(databaseInfoTest, "test_people"));
                });
        }

        void MakeTableWithMultiColumnUnique(
            Transaction& transaction,
            TestDatabaseUtil& testDatabaseUtil) {
            auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            constexpr std::string_view kProducts = "test_products";
            databaseInfo.AddTable(kProducts);
            databaseInfo.AddColumnPrimaryKey(kProducts, "product_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnSimple(kProducts, "sku", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kProducts, "region", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kProducts, "warehouse", DB_TYPE_STRING);
            databaseInfo.AddNamedUniqueConstraint(kProducts, "uq_sku_region", "sku", "region");
            databaseInfo.AddUniqueConstraint(kProducts, "region", "warehouse");
            DbOps::CreateTable(transaction, databaseInfo, kProducts);
        }

        TEST(DatabaseMetadataTest, ListTableUniqueValuesArrayForTableBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListTableUniqueValuesArrayForTableBasic",
                [&](Transaction& transaction) {
                    MakeTableWithMultiColumnUnique(transaction, testDatabaseUtil);

                    DbSchema::TableUniqueValuesArray result =
                        ListTableUniqueValuesArrayForTable(transaction, "test_products");

                    ASSERT_EQ(result.size(), 2);
                    // Results are ordered by constraint name
                    // Named constraint should be there
                    bool foundNamedConstraint = false;
                    bool foundUnnamedConstraint = false;
                    for (const auto& tuv : result) {
                        if (tuv.name == "uq_sku_region") {
                            foundNamedConstraint = true;
                            EXPECT_THAT(tuv.columns, ElementsAre("sku", "region"));
                        }
                        else {
                            foundUnnamedConstraint = true;
                            EXPECT_THAT(tuv.columns, ElementsAre("region", "warehouse"));
                        }
                    }
                    ASSERT_TRUE(foundNamedConstraint);
                    ASSERT_TRUE(foundUnnamedConstraint);
                });
        }

        TEST(DatabaseMetadataTest, ListTableUniqueValuesArrayForTableEmpty)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListTableUniqueValuesArrayForTableEmpty",
                [&](Transaction& transaction) {
                    MakePeopleTable(transaction, testDatabaseUtil);

                    // test_people table has single-column unique on email, which should NOT be returned
                    DbSchema::TableUniqueValuesArray result =
                        ListTableUniqueValuesArrayForTable(transaction, "test_people");

                    ASSERT_EQ(result.size(), 0);
                });
        }

        TEST(DatabaseMetadataTest, ListTableUniqueValuesArrayForTableSingleColumnExcluded)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListTableUniqueValuesArrayForTableSingleColumnExcluded",
                [&](Transaction& transaction) {
                    // Create a table with a single-column unique constraint
                    auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
                    constexpr std::string_view kItems = "items";
                    databaseInfo.AddTable(kItems);
                    databaseInfo.AddColumnPrimaryKey(kItems, "item_id", DB_TYPE_SERIAL);
                    databaseInfo.AddColumnUnique(kItems, "code", DB_TYPE_STRING);
                    DbOps::CreateTable(transaction, databaseInfo, kItems);

                    DbSchema::TableUniqueValuesArray result =
                        ListTableUniqueValuesArrayForTable(transaction, "items");

                    // Single-column unique should NOT be returned
                    ASSERT_EQ(result.size(), 0);
                });
        }

        TEST(DatabaseMetadataTest, DatabaseInfoFromDatabaseWithUniqueConstraints)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DatabaseInfoFromDatabaseWithUniqueConstraints",
                [&](Transaction& transaction) {
                    DbSchema::DatabaseInfo databaseInfo("knotty_yoga_test");

                    constexpr std::string_view kProducts = "test_products";
                    databaseInfo.AddTable(kProducts);
                    databaseInfo.AddColumnPrimaryKey(kProducts, "product_id", DB_TYPE_SERIAL);
                    databaseInfo.AddColumnSimple(kProducts, "sku", DB_TYPE_STRING);
                    databaseInfo.AddColumnSimple(kProducts, "region", DB_TYPE_STRING);
                    databaseInfo.AddNamedUniqueConstraint(kProducts, "uq_sku_region", "sku", "region");
                    DbOps::CreateTable(transaction, databaseInfo, kProducts);

                    DbSchema::DatabaseInfo databaseInfoTest = DatabaseInfoFromDatabase(
                        transaction, "knotty_yoga_test", { "test_products" });

                    // Verify the unique constraint was reverse-engineered
                    ASSERT_EQ(
                        databaseInfoTest.GetTableInfo("test_products").GetTableUniqueValues().size(),
                        1);
                    ASSERT_EQ(
                        databaseInfoTest.GetTableInfo("test_products").GetTableUniqueValues()[0].name,
                        "uq_sku_region");
                    EXPECT_THAT(
                        databaseInfoTest.GetTableInfo("test_products").GetTableUniqueValues()[0].columns,
                        ElementsAre("sku", "region"));

                    // Verify the DDL matches
                    ASSERT_EQ(
                        DbOps::GenerateCreateTableSql(databaseInfo, "test_products"),
                        DbOps::GenerateCreateTableSql(databaseInfoTest, "test_products"));
                });
        }

        TEST(DatabaseMetadataTest, IsForeignKeyNullableBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("IsForeignKeyNullableBasic", [&](Transaction& transaction) {
                // Create parent table
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("test_parent");
                info.AddColumnPrimaryKey("test_parent", "id", DB_TYPE_BIGSERIAL);
                DbOps::CreateTable(transaction, info, "test_parent");

                // Create child with nullable FK
                info.AddTable("test_child_nullable");
                info.AddColumnPrimaryKey("test_child_nullable", "id", DB_TYPE_BIGSERIAL);
                info.AddColumnForeignKeyRefNullable(
                    "test_parent", "id", "test_child_nullable", "parent_id");
                DbOps::CreateTable(transaction, info, "test_child_nullable");

                // Create child with non-nullable FK
                info.AddTable("test_child_notnull");
                info.AddColumnPrimaryKey("test_child_notnull", "id", DB_TYPE_BIGSERIAL);
                info.AddColumnForeignKeyRef(
                    "test_parent", "id", "test_child_notnull", "parent_id");
                DbOps::CreateTable(transaction, info, "test_child_notnull");

                EXPECT_TRUE(IsForeignKeyNullable(
                    transaction, "test_child_nullable", "parent_id"));
                EXPECT_FALSE(IsForeignKeyNullable(
                    transaction, "test_child_notnull", "parent_id"));
                });
        }

        TEST(DatabaseMetadataTest, DatabaseInfoFromDatabaseWithNullableForeignKey)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DatabaseInfoFromDatabaseNullableFK", [&](Transaction& transaction) {
                // Create tables with nullable FK
                DbSchema::DatabaseInfo databaseInfo("knotty_yoga_test");

                constexpr std::string_view kParent = "test_parent";
                databaseInfo.AddTable(kParent);
                databaseInfo.AddColumnPrimaryKey(kParent, "id", DB_TYPE_BIGSERIAL);
                DbOps::CreateTable(transaction, databaseInfo, kParent);

                constexpr std::string_view kChild = "test_child";
                databaseInfo.AddTable(kChild);
                databaseInfo.AddColumnPrimaryKey(kChild, "id", DB_TYPE_BIGSERIAL);
                databaseInfo.AddColumnForeignKeyRefNullable(
                    kParent, "id", kChild, "parent_id");
                DbOps::CreateTable(transaction, databaseInfo, kChild);

                StringArray allowedTables = { "test_parent", "test_child" };
                DbSchema::DatabaseInfo databaseInfoTest = DatabaseInfoFromDatabase(
                    transaction, "knotty_yoga_test", allowedTables);

                // Verify FK is marked as nullable in the introspected schema
                EXPECT_TRUE(databaseInfoTest.GetForeignKeyManager().IsNullable(
                    { "test_parent", "id" }, { "test_child", "parent_id" }));

                // Verify the column is nullable
                EXPECT_TRUE(databaseInfoTest.GetTableInfo("test_child")
                    .GetColumn("parent_id").IsNullable());
                });
        }

        // Phase 1.6 of the security review.
        TEST(DatabaseMetadataTest, DatabaseTypesFromDatabaseColumnInfoCitextViaPrimaryLookup)
        {
            // Some Postgres paths emit the type name directly in data_type
            // (e.g. format_type from pg_attribute). The primary lookup must
            // resolve it without consulting udt_name.
            DatabaseColumnInfo info;
            info.dataType = "citext";
            info.udtName = "citext";
            EXPECT_EQ(DatabaseTypesFromDatabaseColumnInfo(info), DB_TYPE_CITEXT);
        }

        TEST(DatabaseMetadataTest, DatabaseTypesFromDatabaseColumnInfoCitextViaUdtNameFallback)
        {
            // information_schema reports user-defined types with
            // data_type='USER-DEFINED' and the real type name in udt_name.
            // The fallback path must resolve it.
            DatabaseColumnInfo info;
            info.dataType = "USER-DEFINED";
            info.udtName = "citext";
            EXPECT_EQ(DatabaseTypesFromDatabaseColumnInfo(info), DB_TYPE_CITEXT);
        }

        TEST(DatabaseMetadataTest, DatabaseTypesFromDatabaseColumnInfoUnknownUserDefinedThrows)
        {
            // An unrecognized user-defined type should fail loudly with a
            // message that names the type, so the next maintainer knows
            // exactly what to add to kUserDefinedLookup.
            DatabaseColumnInfo info;
            info.dataType = "USER-DEFINED";
            info.udtName = "unknown_user_defined_type";
            try {
                DatabaseTypesFromDatabaseColumnInfo(info);
                FAIL() << "expected std::invalid_argument";
            } catch (const std::invalid_argument& e) {
                std::string msg = e.what();
                EXPECT_NE(msg.find("unknown_user_defined_type"),
                    std::string::npos)
                    << "exception text should name the type, got: " << msg;
            }
        }

        TEST(DatabaseMetadataTest, ListColumnsCitextRoundTrip)
        {
            // Create a table with a CITEXT column and confirm the metadata
            // reader maps it back to DB_TYPE_CITEXT. Exercises the path used
            // by DatabaseInfoFromDatabase against a live Postgres catalog.
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("ListColumnsCitextRoundTrip",
                [&](Transaction& transaction) {
                    auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
                    constexpr std::string_view kCitextTable = "test_citext_email";
                    databaseInfo.AddTable(kCitextTable);
                    databaseInfo.AddColumnPrimaryKey(
                        kCitextTable, "id", DB_TYPE_BIGSERIAL);
                    databaseInfo.AddColumnUnique(
                        kCitextTable, "email", DB_TYPE_CITEXT);
                    DbOps::CreateTable(transaction, databaseInfo, kCitextTable);

                    DatabaseColumnInfoArray columns =
                        ListColumns(transaction, kCitextTable);
                    bool foundEmail = false;
                    for (const auto& column : columns) {
                        if (column.columnName == "email") {
                            foundEmail = true;
                            EXPECT_EQ(
                                DatabaseTypesFromDatabaseColumnInfo(column),
                                DB_TYPE_CITEXT);
                        }
                    }
                    EXPECT_TRUE(foundEmail);
                });
        }

        TEST(DatabaseMetadataTest, DatabaseInfoFromDatabaseWithCitextColumn)
        {
            // Higher-level round-trip: DatabaseInfoFromDatabase should
            // reconstruct a DatabaseInfo whose email column has
            // DB_TYPE_CITEXT.
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DatabaseInfoFromDatabaseWithCitextColumn",
                [&](Transaction& transaction) {
                    auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
                    constexpr std::string_view kCitextTable = "test_citext_round_trip";
                    databaseInfo.AddTable(kCitextTable);
                    databaseInfo.AddColumnPrimaryKey(
                        kCitextTable, "id", DB_TYPE_BIGSERIAL);
                    databaseInfo.AddColumnUnique(
                        kCitextTable, "email", DB_TYPE_CITEXT);
                    DbOps::CreateTable(transaction, databaseInfo, kCitextTable);

                    DbSchema::DatabaseInfo introspected = DatabaseInfoFromDatabase(
                        transaction, "knotty_yoga_test",
                        { std::string(kCitextTable) });

                    EXPECT_EQ(
                        introspected.GetTableInfo(kCitextTable)
                            .GetColumn("email").GetDatabaseType(),
                        DB_TYPE_CITEXT);
                });
        }

    } // namespace
}  // namespace DbMeta
