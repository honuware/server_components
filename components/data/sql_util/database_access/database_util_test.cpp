#include "sql_util/database_access/database_util.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/file_util.h"
#include "test/src/util/database_helper_test.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbUtil {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::ElementsAre;
        using ::testing::Eq;
        using ::testing::Not;

        constexpr std::string_view kTestDatabase = "database_test_ddl";

        void MakePeopleTable(Transaction& transaction) {
            DbSchema::DatabaseInfo databaseInfo(kTestDatabase);
            constexpr std::string_view kPeople = "people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "first_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "last_name", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "password_hash", DB_TYPE_STRING);
            DbOps::CreateTable(transaction, databaseInfo, kPeople);
        }

        TEST(DatabaseUtilTest, GenerateSelectStatementFromTableAndInfoBasic)
        {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray = {
                {"column1", 0, "mytype", 0, "mytype", false},
                {"column2", 1, "mytype", 0, "mytype", false}
            };
            EXPECT_THAT(
                GenerateSelectStatementFromTableAndInfo(
                    "mytable", databaseColumnInfoArray),
                Eq("SELECT column1, column2 FROM mytable"));
        }

        TEST(DatabaseUtilTest, MakeAndClearDatabaseBasic) {
            DatabaseHelper noDatabaseHelper = MakeNoDatabaseHelper();
            noDatabaseHelper.RunInTransaction("MakeAndClearDatabaseBasic", [&](Transaction& transaction) {
                ClearDatabase(transaction, kTestDatabase);
                EXPECT_THAT(DbMeta::ListDatabases(transaction),
                    Not(Contains(std::string(kTestDatabase))));
                MakeDatabase(transaction, kTestDatabase);
                EXPECT_THAT(DbMeta::ListDatabases(transaction),
                    Contains(std::string(kTestDatabase)));
                ClearDatabase(transaction, kTestDatabase);
                EXPECT_THAT(DbMeta::ListDatabases(transaction),
                    Not(Contains(std::string(kTestDatabase))));
                });
        }

        TEST(DatabaseUtilTest, RemoveAndCreateDatabaseBasic) {
            DatabaseHelper noDatabaseHelper = MakeNoDatabaseHelper();
            noDatabaseHelper.RunInTransaction("RemoveAndCreateDatabaseBasic", [&](Transaction& transaction) {
                ClearDatabase(transaction, kTestDatabase);
                MakeDatabase(transaction, kTestDatabase);
                });
            {
                DatabaseHelper databaseHelper = MakeProductionDatabaseHelper(kTestDatabase);
                databaseHelper.RunInTransaction("CheckDatabaseExists", [&](Transaction& transaction) {
                    // Scope this database helper so that it isn't holding a connection
                    // to the database when we try to remove it.
                    MakePeopleTable(transaction);

                    EXPECT_THAT(
                        DbMeta::ListTables(transaction), Contains("people"));
                    });
            }
            noDatabaseHelper.RunInTransaction("RemoveAndCreateDatabaseBasic", [&](Transaction& transaction) {
                RemoveAndCreateDatabase(transaction, kTestDatabase);
                });
            {
                DatabaseHelper databaseHelper = MakeProductionDatabaseHelper(kTestDatabase);
                databaseHelper.RunInTransaction("CheckDatabaseExists", [&](Transaction& transaction) {
                    EXPECT_THAT(
                        DbMeta::ListTables(transaction), Not(Contains("people")));
                    });
            }
            noDatabaseHelper.RunInTransaction("RemoveAndCreateDatabaseBasic", [&](Transaction& transaction) {
                ClearDatabase(transaction, kTestDatabase);
                });
        }

        TEST(DatabaseUtilTest, RunSqlFileBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementBasic", [&](Transaction& transaction) {
                RunSqlFile(
                    transaction,
                    // Phase 3.1: database_access (and this test's SQL fixture)
                    // moved from src/ to components/data/.
                    "../components/data/sql_util/database_access/test/",
                    "create_table.sql");
                EXPECT_THAT(DbMeta::ListTables(transaction),
                    Contains("people"));
                });
        }

    } // namespace
}  // namespace DbUtil