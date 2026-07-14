#include "table_info.h"

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

        TEST(TableInfoTest, AddColumnIsColumnBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo("column1", DB_TYPE_INT);
            ASSERT_FALSE(info.IsColumn("column1"));
            info.AddColumn(columnInfo);
            ASSERT_TRUE(info.IsColumn("column1"));
        }

        TEST(TableInfoTest, AddColumnDuplicate)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo("column1", DB_TYPE_INT);
            info.AddColumn(columnInfo);
            try {
                info.AddColumn(columnInfo);
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "TableInfoImpl::AddColumn - column(column1) already exists."
                );
            }
        }

        TEST(TableInfoTest, GetColumnsBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT);
            EXPECT_THAT(info.GetColumns(), UnorderedElementsAre());
            info.AddColumn(columnInfo1);
            EXPECT_THAT(info.GetColumns(), UnorderedElementsAre(columnInfo1));
            info.AddColumn(columnInfo2);
            EXPECT_THAT(
                info.GetColumns(),
                UnorderedElementsAre(columnInfo1, columnInfo2));
        }

        TEST(TableInfoTest, IsPrimaryKeyBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT, true);
            info.AddColumn(columnInfo1);
            ASSERT_FALSE(info.IsPrimaryKey());
            info.AddColumn(columnInfo2);
            ASSERT_TRUE(info.IsPrimaryKey());
        }

        TEST(TableInfoTest, GetPrimaryKeyBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT, true);
            info.AddColumn(columnInfo1);
            info.AddColumn(columnInfo2);
            ASSERT_EQ(info.GetPrimaryKey(), "column2");
        }

        TEST(TableInfoTest, GetPrimaryKeyNotPresent)
        {
            TableInfo info("table1");
            try {
                info.GetPrimaryKey();
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "TableInfoImpl::GetPrimaryKey table(table1) has"
                    " no primary key.");
            }
        }

        TEST(TableInfoTest, IsColumnBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            ASSERT_FALSE(info.IsColumn("column1"));
            info.AddColumn(columnInfo1);
            ASSERT_TRUE(info.IsColumn("column1"));
        }

        TEST(TableInfoTest, GetColumnBasic)
        {
            TableInfo info("table1");
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            info.AddColumn(columnInfo1);
            ASSERT_EQ(info.GetColumn("column1"), columnInfo1);
        }

        TEST(TableInfoTest, GetColumnNotFound)
        {
            TableInfo info("table1");
            try {
                info.GetColumn("column1");
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "TableInfoImpl::GetColumn column(column1) not found.");
            }
        }

        TEST(TableInfoTest, GetTableNameBasic)
        {
            TableInfo info("table1");
            ASSERT_EQ(info.GetTableName(), "table1");
        }

        TEST(TableInfoTest, BuildTableUniqueValuesBasic)
        {
            TableUniqueValues tuv = BuildTableUniqueValues("my_constraint", "col1", "col2");
            ASSERT_EQ(tuv.name, "my_constraint");
            EXPECT_THAT(tuv.columns, ElementsAreArray({"col1", "col2"}));
        }

        TEST(TableInfoTest, BuildTableUniqueValuesEmptyName)
        {
            TableUniqueValues tuv = BuildTableUniqueValues("", "col1", "col2");
            ASSERT_EQ(tuv.name, "");
            EXPECT_THAT(tuv.columns, ElementsAreArray({"col1", "col2"}));
        }

        TEST(TableInfoTest, AddUniqueConstraintHelperBasic)
        {
            TableInfo info("table1");
            TableUniqueValues tuv;
            tuv.name = "uq_test";
            tuv.columns = {"col1", "col2"};
            info.AddUniqueConstraintHelper(tuv);

            ASSERT_EQ(info.GetTableUniqueValues().size(), 1);
            ASSERT_EQ(info.GetTableUniqueValues()[0].name, "uq_test");
            EXPECT_THAT(info.GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
        }

        TEST(TableInfoTest, AddUniqueConstraintBasic)
        {
            TableInfo info("table1");
            info.AddUniqueConstraint("col1", "col2", "col3");

            ASSERT_EQ(info.GetTableUniqueValues().size(), 1);
            ASSERT_EQ(info.GetTableUniqueValues()[0].name, "");
            EXPECT_THAT(info.GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2", "col3"}));
        }

        TEST(TableInfoTest, AddNamedUniqueConstraintBasic)
        {
            TableInfo info("table1");
            info.AddNamedUniqueConstraint("my_unique_constraint", "col1", "col2");

            ASSERT_EQ(info.GetTableUniqueValues().size(), 1);
            ASSERT_EQ(info.GetTableUniqueValues()[0].name, "my_unique_constraint");
            EXPECT_THAT(info.GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
        }

        TEST(TableInfoTest, AddUniqueConstraintNoColumns)
        {
            TableInfo info("table1");
            TableUniqueValues tuv;
            tuv.name = "uq_empty";
            // columns is empty
            try {
                info.AddUniqueConstraintHelper(tuv);
                FAIL() << "Expected exception was not thrown";
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "TableInfoImpl::AddUniqueConstraintHelper - "
                    "unique constraint must have at least one column.");
            }
        }

        TEST(TableInfoTest, GetTableUniqueValuesEmpty)
        {
            TableInfo info("table1");
            ASSERT_EQ(info.GetTableUniqueValues().size(), 0);
        }

        TEST(TableInfoTest, GetTableUniqueValuesMultiple)
        {
            TableInfo info("table1");
            info.AddUniqueConstraint("col1", "col2");
            info.AddNamedUniqueConstraint("named_uq", "col3", "col4");

            ASSERT_EQ(info.GetTableUniqueValues().size(), 2);
            ASSERT_EQ(info.GetTableUniqueValues()[0].name, "");
            EXPECT_THAT(info.GetTableUniqueValues()[0].columns,
                ElementsAreArray({"col1", "col2"}));
            ASSERT_EQ(info.GetTableUniqueValues()[1].name, "named_uq");
            EXPECT_THAT(info.GetTableUniqueValues()[1].columns,
                ElementsAreArray({"col3", "col4"}));
        }

        TEST(TableInfoTest, TableInfoEqualityWithUniqueConstraints)
        {
            TableInfo info1("table1");
            info1.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            info1.AddUniqueConstraint("col1");

            TableInfo info2("table1");
            info2.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            info2.AddUniqueConstraint("col1");

            TableInfo info3("table1");
            info3.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            // No unique constraint

            ASSERT_TRUE(info1 == info2);
            ASSERT_FALSE(info1 == info3);
        }

        TEST(TableInfoTest, AddCheckConstraintBasic)
        {
            TableInfo info("table1");
            info.AddCheckConstraint("chk_positive", "col1 > 0");

            ASSERT_EQ(info.GetTableCheckConstraints().size(), 1);
            ASSERT_EQ(info.GetTableCheckConstraints()[0].name, "chk_positive");
            ASSERT_EQ(info.GetTableCheckConstraints()[0].expression, "col1 > 0");
        }

        TEST(TableInfoTest, AddCheckConstraintUnnamed)
        {
            TableInfo info("table1");
            info.AddCheckConstraint("", "col1 IS NOT NULL OR col2 IS NOT NULL");

            ASSERT_EQ(info.GetTableCheckConstraints().size(), 1);
            ASSERT_EQ(info.GetTableCheckConstraints()[0].name, "");
            ASSERT_EQ(
                info.GetTableCheckConstraints()[0].expression,
                "col1 IS NOT NULL OR col2 IS NOT NULL");
        }

        TEST(TableInfoTest, AddCheckConstraintEmptyExpression)
        {
            TableInfo info("table1");
            try {
                info.AddCheckConstraint("chk_empty", "");
                FAIL() << "Expected exception was not thrown";
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "TableInfoImpl::AddCheckConstraint - "
                    "check constraint must have a non-empty expression.");
            }
        }

        TEST(TableInfoTest, GetTableCheckConstraintsEmpty)
        {
            TableInfo info("table1");
            ASSERT_EQ(info.GetTableCheckConstraints().size(), 0);
        }

        TEST(TableInfoTest, GetTableCheckConstraintsMultiple)
        {
            TableInfo info("table1");
            info.AddCheckConstraint("chk_one", "col1 > 0");
            info.AddCheckConstraint("", "col2 < 10");

            ASSERT_EQ(info.GetTableCheckConstraints().size(), 2);
            ASSERT_EQ(info.GetTableCheckConstraints()[0].name, "chk_one");
            ASSERT_EQ(info.GetTableCheckConstraints()[0].expression, "col1 > 0");
            ASSERT_EQ(info.GetTableCheckConstraints()[1].name, "");
            ASSERT_EQ(info.GetTableCheckConstraints()[1].expression, "col2 < 10");
        }

        TEST(TableInfoTest, TableInfoEqualityWithCheckConstraints)
        {
            TableInfo info1("table1");
            info1.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            info1.AddCheckConstraint("chk_positive", "col1 > 0");

            TableInfo info2("table1");
            info2.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            info2.AddCheckConstraint("chk_positive", "col1 > 0");

            TableInfo info3("table1");
            info3.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            // No check constraint

            TableInfo info4("table1");
            info4.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            info4.AddCheckConstraint("chk_positive", "col1 >= 0");

            ASSERT_TRUE(info1 == info2);
            ASSERT_FALSE(info1 == info3);
            ASSERT_FALSE(info1 == info4);
        }

        TEST(TableInfoTest, CloneIsIndependentDeepCopy)
        {
            TableInfo original("table1");
            original.AddColumn(ColumnInfo("col1", DB_TYPE_INT));
            original.AddCheckConstraint("chk_positive", "col1 > 0");

            TableInfo clone = original.Clone();

            // The clone starts equal to the original (same columns/constraints).
            EXPECT_TRUE(clone == original);

            // Adding a column to the clone does not affect the original.
            clone.AddColumn(ColumnInfo("clone_col", DB_TYPE_STRING));
            EXPECT_TRUE(clone.IsColumn("clone_col"));
            EXPECT_FALSE(original.IsColumn("clone_col"));

            // Adding a check constraint to the original does not affect the clone.
            original.AddCheckConstraint("chk_two", "col1 < 100");
            EXPECT_EQ(original.GetTableCheckConstraints().size(), 2);
            EXPECT_EQ(clone.GetTableCheckConstraints().size(), 1);
        }

    } // namespace {
} //  namespace DbSchema {
