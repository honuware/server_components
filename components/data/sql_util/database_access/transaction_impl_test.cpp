#include "sql_util/database_access/transaction_impl.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/people.h"
#include "sql_util/database_common.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "test/src/util/test_helper.h"
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

        int64_t AddPerson(Transaction& transaction, DatabaseHelper databaseHelper, std::string_view email,
            std::string_view first_name, std::string_view last_name,
            std::string_view password_hash) {
            return DbCrud::AddRowToTableFetchInt64PrimaryKey(
                transaction,
                databaseHelper,
                "test_people",
                {
                    DbPair("email", email),
                    DbPair("first_name", first_name),
                    DbPair("last_name", last_name),
                    DbPair("password_hash", password_hash)
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningStringTableBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningStringTableBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                EXPECT_THAT(DbMeta::ListTables(transaction), Contains("test_people"));

                int64_t id1 = AddPerson(transaction, databaseHelper, "person1@hotmail.com",
                    "first1", "last1", "hash1");
                int64_t id2 = AddPerson(transaction, databaseHelper, "person2@hotmail.com",
                    "first2", "last2", "hash2");

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id1), "person1@hotmail.com",
                            "first1", "last1", "hash1"),
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com",
                            "first2", "last2", "hash2")
                    ));
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningOneValueBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningOneValueBasic", [&](Transaction& transaction) {
                std::string value = transaction.RunSqlStatementReturningOneValue("select 2 + 3");
                ASSERT_EQ(value, "5");
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningDataResultsBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningDataResultsBasic", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                int64_t id1 = AddPerson(transaction, db, "a@example.com", "Alice", "A", "h1");
                int64_t id2 = AddPerson(transaction, db, "b@example.com", "Bob", "B", "h2");

                DataResults dr = transaction.RunSqlStatementReturningDataResults(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE person_id = $1 OR person_id = $2 ORDER BY person_id ASC",
                    StringFromInt(id1), StringFromInt(id2));

                EXPECT_THAT(
                    dr.sortedColumnNames,
                    ElementsAre("email", "first_name", "last_name", "password_hash", "person_id"));

                ASSERT_EQ(dr.dataTable.size(), 2u);
                EXPECT_THAT(
                    dr.dataTable[0],
                    ElementsAre("a@example.com", "Alice", "A", "h1", StringFromInt(id1)));
                EXPECT_THAT(
                    dr.dataTable[1],
                    ElementsAre("b@example.com", "Bob", "B", "h2", StringFromInt(id2)));
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningDataResultsNoResults) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningDataResultsNoResults", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                DataResults dr = transaction.RunSqlStatementReturningDataResults(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE email = $1 ORDER BY person_id ASC",
                    "none@example.com");

                EXPECT_THAT(
                    dr.sortedColumnNames,
                    ElementsAre("email", "first_name", "last_name", "password_hash", "person_id"));
                EXPECT_TRUE(dr.dataTable.empty());
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningKeyValueTableArrayBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningDataResultsBasic", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                int64_t id1 = AddPerson(transaction, db, "a@example.com", "Alice", "A", "h1");
                int64_t id2 = AddPerson(transaction, db, "b@example.com", "Bob", "B", "h2");

                KeyValueTableArray keyValueTableArray = transaction.RunSqlStatementReturningKeyValueTableArray(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE person_id = $1 OR person_id = $2 ORDER BY person_id ASC",
                    StringFromInt(id1), StringFromInt(id2));

                KeyValueTableArray keyValueTableArrayComp = {
                    {
                        {"person_id", StringFromInt(id1)},
                        {"email", "a@example.com"},
                        {"first_name", "Alice"},
                        {"last_name", "A"},
                        {"password_hash", "h1"},
                        {"ignore1", "blah"}
                    },
                    {
                        {"person_id", StringFromInt(id2)},
                        {"email", "b@example.com"},
                        {"first_name", "Bob"},
                        {"last_name", "B"},
                        {"password_hash", "h2"},
                        {"ignore2", "blah"}
                    }
                };

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    keyValueTableArray,
                    keyValueTableArrayComp,
                    "ignore1",
                    "ignore2"));
                ASSERT_FALSE(CompareKeyValueTableArraysMinusColumns(
                    keyValueTableArray,
                    keyValueTableArrayComp));
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningKeyValueTableArrayNoResults) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningDataResultsNoResults", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTableArray keyValueTableArray = transaction.RunSqlStatementReturningKeyValueTableArray(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE email = $1 ORDER BY person_id ASC",
                    "none@example.com");

                EXPECT_TRUE(keyValueTableArray.empty());
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningOneRowBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningOneRowBasic", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                int64_t id = AddPerson(transaction, db, "x@example.com", "X", "Y", "hashx");

                KeyValueTable kv = transaction.RunSqlStatementReturningOneRow(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE person_id = $1",
                    StringFromInt(id));

                ASSERT_FALSE(kv.empty());
                EXPECT_EQ(kv["person_id"], StringFromInt(id));
                EXPECT_EQ(kv["email"], "x@example.com");
                EXPECT_EQ(kv["first_name"], "X");
                EXPECT_EQ(kv["last_name"], "Y");
                EXPECT_EQ(kv["password_hash"], "hashx");
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementReturningOneRowNoResults) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementReturningOneRowNoResults", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable kv = transaction.RunSqlStatementReturningOneRow(
                    "SELECT person_id, email, first_name, last_name, password_hash "
                    "FROM test_people WHERE person_id = $1",
                    "999999");

                EXPECT_TRUE(kv.empty());
                });
        }

        TEST(DatabaseUtilTest, RunSqlStatementBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("RunSqlStatementBasic", [&](Transaction& transaction) {
                DatabaseHelper db = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, db, "p@example.com", "Before", "Last", "h");

                transaction.RunSqlStatement(
                    "UPDATE test_people SET first_name = $1 WHERE email = $2",
                    "After", "p@example.com");

                std::string first = transaction.RunSqlStatementReturningOneValue(
                    "SELECT first_name FROM test_people WHERE email = $1",
                    "p@example.com");
                EXPECT_EQ(first, "After");
                });
        }

    } // namespace
}  // namespace DbUtil