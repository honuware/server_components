#include "column_info.h"

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

        TEST(ColumnInfoTest, GetColumnNameBasic)
        {
            ColumnInfo columnInfo("column1", DB_TYPE_INT);
            ASSERT_EQ(columnInfo.GetColumnName(), "column1");
        }

        TEST(ColumnInfoTest, GetDatabaseTypeBasic)
        {
            ColumnInfo columnInfo("column1", DB_TYPE_INT);
            ASSERT_EQ(columnInfo.GetDatabaseType(), DB_TYPE_INT);
        }

        TEST(ColumnInfoTest, IsPrimaryKeyBasic)
        {
            ColumnInfo columnInfo1("column1", DB_TYPE_INT);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT, true);
            ASSERT_FALSE(columnInfo1.IsPrimaryKey());
            ASSERT_TRUE(columnInfo2.IsPrimaryKey());
        }

        TEST(ColumnInfoTest, IsUniqueBasic)
        {
            ColumnInfo columnInfo1("column1", DB_TYPE_INT, false, false);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT, true, true);
            ASSERT_FALSE(columnInfo1.IsUnique());
            ASSERT_TRUE(columnInfo2.IsUnique());
        }

        TEST(ColumnInfoTest, IsNullableBasic)
        {
            ColumnInfo columnInfo1("column1", DB_TYPE_INT, false, false);
            ColumnInfo columnInfo2("column2", DB_TYPE_INT, false, false, true);
            ASSERT_FALSE(columnInfo1.IsNullable());
            ASSERT_TRUE(columnInfo2.IsNullable());
        }

        TEST(ColumnInfoTest, GetSqlTypeBasic)
        {
            ColumnInfo columnInfo1("column1", DB_TYPE_INT, false, false);
            ColumnInfo columnInfo2("column2", DB_TYPE_STRING, true, true);
            ASSERT_EQ(columnInfo1.GetSqlType(), "INT");
            ASSERT_EQ(columnInfo2.GetSqlType(), "VARCHAR");
        }

        TEST(ColumnInfoTest, SqlTypeFromDatabaseTypeBasic)
        {
			ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_INT), "INT");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_SERIAL), "SERIAL");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_BOOL), "BOOL");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_DOUBLE), "FLOAT8");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_STRING), "VARCHAR");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_JSON), "JSON");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_BYTES), "BYTEA");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_DATE), "DATE");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_TIME), "TIME");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_UUID), "UUID");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_NULL), "NULL");
            ASSERT_EQ(ColumnInfo::SqlTypeFromDatabaseType(DB_TYPE_CITEXT), "CITEXT");
        }

        TEST(ColumnInfoTest, GetSqlTypeCitext)
        {
            // Confirms that an instance constructed with DB_TYPE_CITEXT
            // round-trips its SQL type through GetSqlType, matching the
            // pattern of GetSqlTypeBasic.
            ColumnInfo columnInfo("email", DB_TYPE_CITEXT, false, true);
            ASSERT_EQ(columnInfo.GetSqlType(), "CITEXT");
        }

        TEST(ColumnInfoTest, SqlTypeFromDatabaseTypeNotPresent)
        {
            try {
                ColumnInfo::SqlTypeFromDatabaseType(static_cast<DatabaseTypes>(999));
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "ColumnInfo::SqlTypeFromDatabaseType - invalid type: 999");
            }
        }

    } // namespace {
} //  namespace DbSchema {
