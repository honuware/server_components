#include "sql_util/database_access/db_and_table_operations.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_metadata.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/table_matcher.h"

namespace DbOps {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::Eq;
        using ::testing::Contains;
        using ::testing::Not;
        using ::testing::SizeIs;

        TEST(DbOpsTest, GenerateDropTableSqlBasic)
        {
            ASSERT_EQ(
                GenerateDropTableSql("table1"),
                "DROP TABLE IF EXISTS table1 CASCADE");
        }

        TEST(DbOpsTest, GenerateCreateTableSqlBasic)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddColumnUnique(
                "table1", "column2", DB_TYPE_INT);
            info.AddColumnSimple(
                "table1", "column3", DB_TYPE_STRING);
            info.AddColumnNullable(
                "table1", "column4", DB_TYPE_UUID);
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table1"),
                "CREATE TABLE table1 (column1 SERIAL PRIMARY KEY, column2 INT "
                "UNIQUE, column3 VARCHAR NOT NULL, column4 UUID)");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlEmitsCitext)
        {
            // Phase 1.6 of the security review: DB_TYPE_CITEXT must show up
            // as CITEXT in the generated DDL. This is the DSL-side regression
            // that pairs with the metadata round-trip in
            // database_metadata_test.cpp.
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "id", DB_TYPE_BIGSERIAL);
            info.AddColumnUnique(
                "table1", "email", DB_TYPE_CITEXT);
            std::string sql = GenerateCreateTableSql(info, "table1");
            EXPECT_NE(sql.find("email CITEXT UNIQUE"), std::string::npos)
                << sql;
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlSingleColumnUniqueConstraint)
        {
            // Regression for Phase 1.1: a single-column table-level UNIQUE
            // constraint added via AddUniqueConstraint must show up as
            // UNIQUE (col) in the generated DDL. This is the path used by
            // sessions.uuid and device_tokens.uuid (which can't use the
            // column-level AddColumnUnique because they also need defaults).
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_BIGSERIAL);
            info.AddColumnNotNullableWithDefault(
                "table1", "column2", DB_TYPE_UUID,
                DbSchema::kDatabaseInfoDefaultGenRandomUuid);
            info.AddUniqueConstraint("table1", "column2");
            std::string sql = GenerateCreateTableSql(info, "table1");
            EXPECT_NE(sql.find("column2 UUID NOT NULL DEFAULT GEN_RANDOM_UUID()"),
                std::string::npos) << sql;
            EXPECT_NE(sql.find("UNIQUE (column2)"), std::string::npos) << sql;
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlIfNotExists)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(true);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddColumnSimple(
                "table1", "column2", DB_TYPE_STRING);
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table1"),
                "CREATE TABLE IF NOT EXISTS table1 (column1 SERIAL PRIMARY KEY, "
                "column2 VARCHAR NOT NULL)");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlForeignKeyRefBasic)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddTable("table2");
            info.AddColumnForeignKeyRef("table1", "column1", "table2", "column2");
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table2"),
                "CREATE TABLE table2 (column2 INT NOT NULL, CONSTRAINT "
                "fk_table2_column2 FOREIGN KEY(column2) REFERENCES table1"
                "(column1) ON DELETE CASCADE)");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, CreateTableDropTableBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("CreateTableDropTableBasic", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                DatabaseHelper databaseHelper
                    = testDatabaseUtil.GetDatabaseHelper();
                CreateTable(transaction, info, "table1");
                EXPECT_THAT(
                    DbMeta::ListTables(transaction), Contains("table1"));
                DropTable(transaction, "table1");
                EXPECT_THAT(
                    DbMeta::ListTables(transaction), Not(Contains("table1")));
                });
        }

        TEST(DbOpsTest, DropIfExistsAndCreateTableBasic)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DropIfExistsAndCreateTableBasic", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                DatabaseHelper databaseHelper
                    = testDatabaseUtil.GetDatabaseHelper();
                CreateTable(transaction, info, "table1");
                EXPECT_THAT(
                    DbMeta::ListTables(transaction), Contains("table1"));
                EXPECT_THAT(
                    DbMeta::ListColumns(transaction, "table1"), SizeIs(1));
                info.AddColumnSimple("table1", "column2", DB_TYPE_INT);
                DropIfExistsAndCreateTable(transaction, info, "table1");
                EXPECT_THAT(
                    DbMeta::ListColumns(transaction, "table1"), SizeIs(2));
                });
        }

        TEST(DbOpsTest, DropIfExistsAndCreateTableForeignKeyRef)
        {
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddTable("table2");
            info.AddColumnForeignKeyRef("table1", "column1", "table2", "column2");
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DropIfExistsAndCreateTableForeignKeyRef", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper
                    = testDatabaseUtil.GetDatabaseHelper();
                CreateTable(transaction, info, "table1");
                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("table1"));
                CreateTable(transaction, info, "table2");
                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("table1"));
                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("table2"));
                EXPECT_THAT(
                    DbMeta::ListChildTables(transaction, "table1"),
                    UnorderedElementsAre("table2"));
                std::string parentColumn, childColumn;
                DbMeta::GetParentChildForeignKeyColumns(
                    transaction, "table1", "table2", parentColumn, childColumn);
                ASSERT_EQ(parentColumn, "column1");
                ASSERT_EQ(childColumn, "column2");
                });
        }


        TEST(DbOpsTest, GenerateCreateTableSqlNamedCheckConstraint)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddColumnNullable(
                "table1", "column2", DB_TYPE_INT);
            info.AddCheckConstraint(
                "table1", "chk_table1_positive", "column2 > 0");
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table1"),
                "CREATE TABLE table1 (column1 SERIAL PRIMARY KEY, column2 INT, "
                "CONSTRAINT chk_table1_positive CHECK (column2 > 0))");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlUnnamedCheckConstraint)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddColumnNullable(
                "table1", "column2", DB_TYPE_INT);
            info.AddCheckConstraint("table1", "", "column2 > 0");
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table1"),
                "CREATE TABLE table1 (column1 SERIAL PRIMARY KEY, column2 INT, "
                "CHECK (column2 > 0))");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, GenerateCreateTableSqlCheckConstraintAfterOtherConstraints)
        {
            // Check constraints are emitted after FK and unique constraints so
            // a table combining all three stays stable in the generated DDL.
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddTable("table2");
            info.AddColumnForeignKeyRefNullable(
                "table1", "column1", "table2", "column2");
            info.AddColumnNullable(
                "table2", "column3", DB_TYPE_INT);
            info.AddUniqueConstraint("table2", "column3");
            info.AddCheckConstraint(
                "table2", "chk_table2_subject",
                "column2 IS NOT NULL OR column3 IS NOT NULL");
            ASSERT_EQ(
                GenerateCreateTableSql(info, "table2"),
                "CREATE TABLE table2 (column2 INT, column3 INT, "
                "CONSTRAINT fk_table2_column2 FOREIGN KEY(column2) REFERENCES "
                "table1(column1) ON DELETE CASCADE, "
                "UNIQUE (column3), "
                "CONSTRAINT chk_table2_subject CHECK "
                "(column2 IS NOT NULL OR column3 IS NOT NULL))");
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, CreateTableCheckConstraintAcceptsValidRow)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("CreateTableCheckConstraintAcceptsValidRow", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                info.AddColumnNullable(
                    "table1", "column2", DB_TYPE_INT);
                info.AddCheckConstraint(
                    "table1", "chk_table1_positive", "column2 > 0");
                CreateTable(transaction, info, "table1");
                transaction.RunSqlStatement(
                    "INSERT INTO table1 (column2) VALUES (5)");
                ASSERT_EQ(
                    transaction.RunSqlStatementReturningOneValue(
                        "SELECT COUNT(*) FROM table1"),
                    "1");
                });
        }

        TEST(DbOpsTest, CreateTableCheckConstraintRejectsViolatingRow)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("CreateTableCheckConstraintRejectsViolatingRow", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                info.AddColumnNullable(
                    "table1", "column2", DB_TYPE_INT);
                info.AddCheckConstraint(
                    "table1", "chk_table1_positive", "column2 > 0");
                CreateTable(transaction, info, "table1");
                // The violation aborts the transaction, so it is the last
                // statement in this test.
                EXPECT_THROW(
                    transaction.RunSqlStatement(
                        "INSERT INTO table1 (column2) VALUES (0)"),
                    std::exception);
                });
        }

        TEST(DbOpsTest, GenerateCreateTableSqlNullableForeignKeyRef)
        {
            bool wasIfNotExists = IsIfNotExistsMode();
            SetIfNotExistsMode(false);
            DbSchema::DatabaseInfo info("database1");
            info.AddTable("table1");
            info.AddColumnPrimaryKey(
                "table1", "column1", DB_TYPE_SERIAL);
            info.AddTable("table2");
            info.AddColumnPrimaryKey(
                "table2", "id", DB_TYPE_SERIAL);
            info.AddColumnForeignKeyRefNullable(
                "table1", "column1", "table2", "column2");
            std::string sql = GenerateCreateTableSql(info, "table2");
            // Should NOT contain "NOT NULL" for column2 (nullable FK)
            EXPECT_TRUE(sql.find("column2 INT,") != std::string::npos ||
                sql.find("column2 INT)") != std::string::npos);
            EXPECT_TRUE(sql.find("column2 INT NOT NULL") == std::string::npos);
            // Should still have FK constraint
            EXPECT_TRUE(sql.find("FOREIGN KEY(column2) REFERENCES table1(column1)") != std::string::npos);
            SetIfNotExistsMode(wasIfNotExists);
        }

        TEST(DbOpsTest, CreateTableNullableForeignKeyRef)
        {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("CreateTableNullableForeignKeyRef", [&](Transaction& transaction) {
                DbSchema::DatabaseInfo info("database1");
                info.AddTable("table1");
                info.AddColumnPrimaryKey(
                    "table1", "column1", DB_TYPE_SERIAL);
                info.AddTable("table2");
                info.AddColumnPrimaryKey(
                    "table2", "id", DB_TYPE_SERIAL);
                info.AddColumnForeignKeyRefNullable(
                    "table1", "column1", "table2", "column2");
                DatabaseHelper databaseHelper
                    = testDatabaseUtil.GetDatabaseHelper();
                CreateTable(transaction, info, "table1");
                CreateTable(transaction, info, "table2");
                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("table1"));
                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("table2"));
                // Verify FK relationship exists
                EXPECT_THAT(
                    DbMeta::ListChildTables(transaction, "table1"),
                    UnorderedElementsAre("table2"));
                // Verify the FK column is nullable
                EXPECT_TRUE(DbMeta::IsForeignKeyNullable(
                    transaction, "table2", "column2"));
                });
        }

    } // namespace {
} //  namespace DbOps {
