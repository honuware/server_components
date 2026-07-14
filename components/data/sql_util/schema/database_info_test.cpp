#include "sql_util/schema/database_info.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/table_matcher.h"

namespace DbSchema {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::Eq;

        TEST(DatabaseInfoTest, GetDatabaseNameBasic)
        {
            DatabaseInfo info("database1");
            ASSERT_EQ(info.GetDatabaseName(), "database1");
        }

        TEST(DatabaseInfoTest, GetForeignKeyManagerBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddTable("table2");
            info.AddColumnPrimaryKey("table1", "id", DB_TYPE_SERIAL);
            info.AddColumnForeignKeyRef("table1", "id", "table2", "table1_id");
            ASSERT_TRUE(info.GetForeignKeyManager().IsRootTable("table1"));
            ASSERT_FALSE(info.GetForeignKeyManager().IsRootTable("table2"));
        }

        TEST(DatabaseInfoTest, GetTableInfoAddTableBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple(
                "table1", "column1", DB_TYPE_INT);
            ASSERT_EQ(
                info.GetTableInfo("table1")
                    .GetColumn("column1").GetDatabaseType(),
                DB_TYPE_INT);
        }

        TEST(DatabaseInfoTest, GetTableInfoNotPresent)
        {
            DatabaseInfo info("database1");
            try {
                info.GetTableInfo("table1");
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseInfoImpl::GetTableInfo - table not found: table1");
            }
        }

        TEST(DatabaseInfoTest, GetRootTablesBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddTable("table2");
            info.AddColumnPrimaryKey("table1", "id", DB_TYPE_SERIAL);
            info.AddColumnForeignKeyRef("table1", "id", "table2", "table1_id");
            TableInfo table1("table1");
            EXPECT_THAT(info.GetRootTables(), UnorderedElementsAre("table1"));
        }

        TEST(DatabaseInfoTest, GetAllTablesBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddTable("table2");
            info.AddColumnPrimaryKey("table1", "id", DB_TYPE_SERIAL);
            info.AddColumnForeignKeyRef("table1", "id", "table2", "table1_id");
            TableInfo table1("table1");
            TableInfo table2("table2");
            EXPECT_THAT(info.GetAllTables(), UnorderedElementsAre("table1", "table2"));
        }

        TEST(DatabaseInfoTest, AddTableAlreadyExists)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            try {
                info.AddTable("table1");
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseInfoImpl::AddTable - table already exists: table1");
            }
        }

        TEST(DatabaseInfoTest, AddColumnSimpleBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple(
                "table1", "column1", DB_TYPE_INT);
            ASSERT_EQ(
                info.GetTableInfo("table1")
                .GetColumn("column1").GetDatabaseType(),
                DB_TYPE_INT);
        }

        TEST(DatabaseInfoTest, AddColumnPrimaryKeyBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            ASSERT_EQ(
                info.GetTableInfo("table1")
                .GetColumn("column1").GetDatabaseType(),
                DB_TYPE_SERIAL);
            ASSERT_TRUE(
                info.GetTableInfo("table1")
                .GetColumn("column1").IsPrimaryKey());
            ASSERT_TRUE(info.GetTableInfo("table1").IsPrimaryKey());
        }

        TEST(DatabaseInfoTest, AddColumnUniqueBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnUnique(
                "table1", "column1", DB_TYPE_INT);
            ASSERT_EQ(
                info.GetTableInfo("table1")
                .GetColumn("column1").GetDatabaseType(),
                DB_TYPE_INT);
            ASSERT_TRUE(
                info.GetTableInfo("table1")
                .GetColumn("column1").IsUnique());
        }

        TEST(DatabaseInfoTest, AddColumnNullableBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple(
                "table1", "column1", DB_TYPE_INT);
            info.AddColumnNullable(
                "table1", "column2", DB_TYPE_INT);
            ASSERT_EQ(
                info.GetTableInfo("table1")
                .GetColumn("column2").GetDatabaseType(),
                DB_TYPE_INT);
            ASSERT_FALSE(
                info.GetTableInfo("table1")
                .GetColumn("column1").IsNullable());
            ASSERT_TRUE(
                info.GetTableInfo("table1")
                .GetColumn("column2").IsNullable());
        }

        TEST(DatabaseInfoTest, AddColumnForeignKeyRefWithConversion)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddTable("table2");
            info.AddColumnPrimaryKey("table1", "id", DB_TYPE_SERIAL);
            info.AddColumnForeignKeyRef("table1", "id", "table2", "table1_id");
            ASSERT_TRUE(info.GetForeignKeyManager().IsRootTable("table1"));
            ASSERT_FALSE(info.GetForeignKeyManager().IsRootTable("table2"));
            ASSERT_EQ(
                info.GetTableInfo("table2")
                .GetColumn("table1_id").GetDatabaseType(),
                DB_TYPE_INT);
        }

        TEST(DatabaseInfoTest, AddColumnForeignKeyRefNoConversion)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddTable("table2");
            info.AddColumnPrimaryKey("table1", "id", DB_TYPE_STRING);
            info.AddColumnForeignKeyRef("table1", "id", "table2", "table1_id");
            ASSERT_TRUE(info.GetForeignKeyManager().IsRootTable("table1"));
            ASSERT_FALSE(info.GetForeignKeyManager().IsRootTable("table2"));
            ASSERT_EQ(
                info.GetTableInfo("table2")
                .GetColumn("table1_id").GetDatabaseType(),
                DB_TYPE_STRING);
        }

        TEST(DatabaseInfoTest, AddUniqueConstraintHelperBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple("table1", "col1", DB_TYPE_STRING);
            info.AddColumnSimple("table1", "col2", DB_TYPE_STRING);

            TableUniqueValues tuv;
            tuv.name = "uq_test";
            tuv.columns = {"col1", "col2"};
            info.AddUniqueConstraintHelper("table1", tuv);

            ASSERT_EQ(info.GetTableInfo("table1").GetTableUniqueValues().size(), 1);
            ASSERT_EQ(info.GetTableInfo("table1").GetTableUniqueValues()[0].name, "uq_test");
            EXPECT_THAT(
                info.GetTableInfo("table1").GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
        }

        TEST(DatabaseInfoTest, AddUniqueConstraintBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple("table1", "col1", DB_TYPE_STRING);
            info.AddColumnSimple("table1", "col2", DB_TYPE_STRING);

            info.AddUniqueConstraint("table1", "col1", "col2");

            ASSERT_EQ(info.GetTableInfo("table1").GetTableUniqueValues().size(), 1);
            ASSERT_EQ(info.GetTableInfo("table1").GetTableUniqueValues()[0].name, "");
            EXPECT_THAT(
                info.GetTableInfo("table1").GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
        }

        TEST(DatabaseInfoTest, AddNamedUniqueConstraintBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnSimple("table1", "col1", DB_TYPE_STRING);
            info.AddColumnSimple("table1", "col2", DB_TYPE_STRING);

            info.AddNamedUniqueConstraint("table1", "my_constraint", "col1", "col2");

            ASSERT_EQ(info.GetTableInfo("table1").GetTableUniqueValues().size(), 1);
            ASSERT_EQ(
                info.GetTableInfo("table1").GetTableUniqueValues()[0].name,
                "my_constraint");
            EXPECT_THAT(
                info.GetTableInfo("table1").GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
        }

        TEST(DatabaseInfoTest, AddUniqueConstraintTableNotFound)
        {
            DatabaseInfo info("database1");
            // table1 not added
            try {
                info.AddUniqueConstraint("table1", "col1", "col2");
                FAIL() << "Expected exception was not thrown";
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseInfoImpl::GetTableInfo - table not found: table1");
            }
        }

        TEST(DatabaseInfoTest, AddCheckConstraintBasic)
        {
            DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnNullable("table1", "col1", DB_TYPE_INT);

            info.AddCheckConstraint("table1", "chk_positive", "col1 > 0");

            ASSERT_EQ(info.GetTableInfo("table1").GetTableCheckConstraints().size(), 1);
            ASSERT_EQ(
                info.GetTableInfo("table1").GetTableCheckConstraints()[0].name,
                "chk_positive");
            ASSERT_EQ(
                info.GetTableInfo("table1").GetTableCheckConstraints()[0].expression,
                "col1 > 0");
        }

        TEST(DatabaseInfoTest, AddCheckConstraintTableNotFound)
        {
            DatabaseInfo info("database1");
            // table1 not added
            try {
                info.AddCheckConstraint("table1", "chk_positive", "col1 > 0");
                FAIL() << "Expected exception was not thrown";
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "DatabaseInfoImpl::GetTableInfo - table not found: table1");
            }
        }

        TEST(DatabaseInfoTest, AddColumnForeignKeyRefNullable)
        {
            DatabaseInfo info("database1");
            info.AddTable("parent_table");
            info.AddColumnPrimaryKey("parent_table", "id", DB_TYPE_BIGSERIAL);

            info.AddTable("child_table");
            info.AddColumnPrimaryKey("child_table", "id", DB_TYPE_BIGSERIAL);
            info.AddColumnForeignKeyRefNullable(
                "parent_table", "id", "child_table", "parent_id");

            const TableInfo& childTable = info.GetTableInfo("child_table");
            const ColumnInfo& fkColumn = childTable.GetColumn("parent_id");

            EXPECT_TRUE(fkColumn.IsNullable());
            EXPECT_TRUE(info.GetForeignKeyManager().HasForeignReference(
                { "child_table", "parent_id" }));
            EXPECT_TRUE(info.GetForeignKeyManager().IsNullable(
                { "parent_table", "id" }, { "child_table", "parent_id" }));
        }

        TEST(DatabaseInfoTest, CloneIsIndependentDeepCopy)
        {
            DatabaseInfo original("database1");
            original.AddTable("table1");
            original.AddColumnPrimaryKey("table1", "id", DB_TYPE_BIGSERIAL);

            DatabaseInfo clone = original.Clone();

            // The clone starts with the same tables/columns...
            EXPECT_THAT(clone.GetAllTables(), UnorderedElementsAre("table1"));
            EXPECT_EQ(
                clone.GetTableInfo("table1").GetColumn("id").GetDatabaseType(),
                DB_TYPE_BIGSERIAL);

            // ...but adding a table to the clone does not affect the original...
            clone.AddTable("clone_only");
            EXPECT_THAT(
                clone.GetAllTables(),
                UnorderedElementsAre("table1", "clone_only"));
            EXPECT_THAT(original.GetAllTables(), UnorderedElementsAre("table1"));

            // ...and adding a table to the original does not affect the clone.
            original.AddTable("original_only");
            EXPECT_THAT(
                original.GetAllTables(),
                UnorderedElementsAre("table1", "original_only"));
            EXPECT_THAT(
                clone.GetAllTables(),
                UnorderedElementsAre("table1", "clone_only"));
        }

        TEST(DatabaseInfoTest, CloneIsolatesColumnAndForeignKeyMutations)
        {
            // These two mutations touch the pImpl-backed TableInfo and
            // ForeignKeyManager, which copy shallowly by default. A correct deep
            // clone must isolate them from the original.
            DatabaseInfo original("database1");
            original.AddTable("parent");
            original.AddColumnPrimaryKey("parent", "id", DB_TYPE_BIGSERIAL);

            DatabaseInfo clone = original.Clone();

            // Add a column to the SHARED-name table "parent" in the clone only.
            clone.AddColumnSimple("parent", "clone_col", DB_TYPE_STRING);
            EXPECT_TRUE(clone.GetTableInfo("parent").IsColumn("clone_col"));
            // The original's "parent" table must NOT have gained the column.
            EXPECT_FALSE(original.GetTableInfo("parent").IsColumn("clone_col"));

            // Add a foreign-key reference in the clone only.
            clone.AddTable("child");
            clone.AddColumnForeignKeyRef("parent", "id", "child", "parent_id");
            EXPECT_FALSE(clone.GetForeignKeyManager().IsRootTable("child"));
            // The original's FK manager must NOT know about "child" at all
            // (a shallow copy would have leaked this reference into it).
            EXPECT_FALSE(original.GetForeignKeyManager().HasForeignReference(
                { "child", "parent_id" }));
        }

        TEST(DatabaseInfoTest, CloneAllowsReAddingSameTableName)
        {
            // Regression: the harness hands TestDatabaseUtil a shared composed
            // schema; each instance Clone()s it so adding a per-test table (here
            // "test_people") in one clone never collides with another clone.
            DatabaseInfo shared("database1");
            shared.AddTable("existing");

            DatabaseInfo first = shared.Clone();
            DatabaseInfo second = shared.Clone();

            // Both clones can add the same table name without throwing, because
            // each owns an independent impl.
            first.AddTable("test_people");
            second.AddTable("test_people");

            EXPECT_THAT(
                first.GetAllTables(),
                UnorderedElementsAre("existing", "test_people"));
            EXPECT_THAT(
                second.GetAllTables(),
                UnorderedElementsAre("existing", "test_people"));
            // The source schema is untouched.
            EXPECT_THAT(shared.GetAllTables(), UnorderedElementsAre("existing"));
        }

        TEST(DatabaseInfoTest, AddColumnForeignKeyRefNotNullable)
        {
            DatabaseInfo info("database1");
            info.AddTable("parent_table");
            info.AddColumnPrimaryKey("parent_table", "id", DB_TYPE_BIGSERIAL);

            info.AddTable("child_table");
            info.AddColumnPrimaryKey("child_table", "id", DB_TYPE_BIGSERIAL);
            info.AddColumnForeignKeyRef(
                "parent_table", "id", "child_table", "parent_id");

            const TableInfo& childTable = info.GetTableInfo("child_table");
            const ColumnInfo& fkColumn = childTable.GetColumn("parent_id");

            EXPECT_FALSE(fkColumn.IsNullable());
            EXPECT_TRUE(info.GetForeignKeyManager().HasForeignReference(
                { "child_table", "parent_id" }));
            EXPECT_FALSE(info.GetForeignKeyManager().IsNullable(
                { "parent_table", "id" }, { "child_table", "parent_id" }));
        }

    } // namespace {
} //  namespace DbSchema {
