#include "sql_util/database_access/database_crud_helpers.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "database_crud_helpers_priv.h"
#include "sql_util/database_common.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_metadata.h"
#include "util/date_time_util.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "test/src/util/test_helper.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbCrud {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::Pair;
        using ::testing::UnorderedElementsAre;
        using ::testing::ElementsAre;
        using ::testing::Eq;
        using ::testing::Not;

        void MakePeopleTable(Transaction& transaction, TestDatabaseUtil& testDatabaseUtil) {
            auto databaseHelper = testDatabaseUtil.GetDatabaseHelper();
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

        void MakePeopleTableWithTimestamp(Transaction& transaction, TestDatabaseUtil& testDatabaseUtil) {
            auto databaseHelper = testDatabaseUtil.GetDatabaseHelper();
            auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();
            constexpr std::string_view kPeople = "test_people";
            databaseInfo.AddTable(kPeople);
            databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);
            databaseInfo.AddColumnSimple(kPeople, "updated_at", DB_TYPE_TIMESTAMPTZ);
            DbOps::CreateTable(transaction, databaseInfo, kPeople);
        }

        void MakeOrdersTable(Transaction& transaction, TestDatabaseUtil& testDatabaseUtil) {
            auto databaseHelper = testDatabaseUtil.GetDatabaseHelper();
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

        int AddPersonWithTimestamp(Transaction& transaction, DatabaseHelper databaseHelper, std::string_view email) {
            return AddRowToTableFetchIntPrimaryKey(
                transaction,
                databaseHelper,
                "test_people",
                {
                    DbPair("email", email),
                    DbPair("updated_at", "now()")
                });
        }

        int AddPerson(Transaction& transaction, DatabaseHelper databaseHelper, std::string_view email,
            std::string_view first_name, std::string_view last_name,
            std::string_view password_hash) {
            return AddRowToTableFetchIntPrimaryKey(
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

        TEST(DatabaseCrudHelpersTest, GenerateAddRowToTableSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateAddRowToTableSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable = {
                    {"email", "email1"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"}
                };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateAddRowToTableSql(transaction, databaseHelper, "test_people", keyValueTable, StringSet(), execParamsHelpers),
                    "INSERT INTO test_people (email, first_name, last_name, password_hash) VALUES ($1, $2, $3, $4)");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("email1", "first1", "last1", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateAddRowToTableSqlSqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateAddRowToTableSqlSqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable = {
                    {"email", "email1"},
                    {"first_name", "now()"},
                    {"last_name", "check"},
                    {"password_hash", "hash1"}
                };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateAddRowToTableSql(transaction, databaseHelper, "test_people", keyValueTable, { "abc", "now()", "check" }, execParamsHelpers),
                    "INSERT INTO test_people (email, first_name, last_name, password_hash) VALUES ($1, now(), check, $2)");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("email1", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateAddRowToTableFetchIntPrimaryKeySqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateAddRowToTableFetchIntPrimaryKeySqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable = {
                    {"email", "email1"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"}
                };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateAddRowToTableFetchIntPrimaryKeySql(transaction, databaseHelper, "test_people", keyValueTable, StringSet(), execParamsHelpers),
                    "INSERT INTO test_people (email, first_name, last_name, password_hash) VALUES ($1, $2, $3, $4) RETURNING person_id");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("email1", "first1", "last1", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateAddRowToTableFetchIntPrimaryKeySqlSqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateAddRowToTableFetchIntPrimaryKeySqlSqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable = {
                    {"email", "email1"},
                    {"first_name", "now()"},
                    {"last_name", "check"},
                    {"password_hash", "hash1"}
                };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateAddRowToTableFetchIntPrimaryKeySql(transaction, databaseHelper, "test_people", keyValueTable, { "abc", "now()", "check" }, execParamsHelpers),
                    "INSERT INTO test_people (email, first_name, last_name, password_hash) VALUES ($1, now(), check, $2) RETURNING person_id");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("email1", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, AddRowToTableBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddRowToTableBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                AddRowToTable(transaction, databaseHelper, "test_people", keyValueTable1);
                AddRowToTable(transaction, databaseHelper, "test_people", keyValueTable2);

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre("1", "person1@hotmail.com",
                            "first1", "last1", "hash1"),
                        ElementsAre("2", "person2@hotmail.com",
                            "first2", "last2", "hash2")
                    ));
                });
        }

        TEST(DatabaseCrudHelpersTest, AddRowToTableSqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddRowToTableSqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTableWithTimestamp(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"updated_at", "now()"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"updated_at", "now()"} };

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                AddRowToTable(transaction, databaseHelper, "test_people", keyValueTable1, { "now()" });

                StringTable results = transaction.RunSqlStatementReturningStringTable(select_statement);
                ASSERT_EQ(results.size(), 1);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(results[0][0], "1");
                ASSERT_EQ(results[0][1], "person1@hotmail.com");

                AddRowToTable(transaction, databaseHelper, "test_people", keyValueTable2, { "now()" });

                results = transaction.RunSqlStatementReturningStringTable(select_statement); ASSERT_EQ(results.size(), 2);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(results[1].size(), 3);
                int indexFirst = 0;
                int indexSecond = 1;
                if (results[0][0] == "2") {
                    indexFirst = 1;
                    indexSecond = 0;
                }
                ASSERT_EQ(results[indexSecond][0], "2");
                ASSERT_EQ(results[indexSecond][1], "person2@hotmail.com");
                int64_t timestampFirst = DateTimeUtil::StringToEpochMillis(results[indexFirst][2]);
                int64_t timestampSecond = DateTimeUtil::StringToEpochMillis(results[indexSecond][2]);
                ASSERT_TRUE(timestampSecond >= timestampFirst);
                });
        }

        TEST(DatabaseCrudHelpersTest, AddRowToTableFetchIntPrimaryKeyBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddRowToTableFetchIntPrimaryKeyBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id1), "person1@hotmail.com", "first1", "last1", "hash1"),
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last2", "hash2")
                    ));
                });
        }

        TEST(DatabaseCrudHelpersTest, AddRowToTableFetchIntPrimaryKeySqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddRowToTableFetchIntPrimaryKeySqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTableWithTimestamp(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"updated_at", "now()"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"updated_at", "now()"} };

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1, { "now()" });

                StringTable results = transaction.RunSqlStatementReturningStringTable(select_statement);
                ASSERT_EQ(results.size(), 1);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(id1, 1);
                ASSERT_EQ(results[0][0], "1");
                ASSERT_EQ(results[0][1], "person1@hotmail.com");

                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2, { "now()" });

                results = transaction.RunSqlStatementReturningStringTable(select_statement); ASSERT_EQ(results.size(), 2);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(results[1].size(), 3);
                int indexFirst = 0;
                int indexSecond = 1;
                if (results[0][0] == "2") {
                    indexFirst = 1;
                    indexSecond = 0;
                }
                ASSERT_EQ(id2, 2);
                ASSERT_EQ(results[indexSecond][0], "2");
                ASSERT_EQ(results[indexSecond][1], "person2@hotmail.com");
                int64_t timestampFirst = DateTimeUtil::StringToEpochMillis(results[indexFirst][2]);
                int64_t timestampSecond = DateTimeUtil::StringToEpochMillis(results[indexSecond][2]);
                ASSERT_TRUE(timestampSecond >= timestampFirst);
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateLookupRowByValueSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateLookupRowByValueSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                EXPECT_EQ(
                    PrivateSql::GenerateLookupRowByValueSql(transaction, databaseHelper, "test_people", "person_id", "1"),
                    "SELECT person_id, email, first_name, last_name, password_hash FROM test_people WHERE person_id = $1");
                });
        }

        TEST(DatabaseCrudHelpersTest, LookupRowByValueBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("LookupRowByValueBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                EXPECT_THAT(
                    LookupRowByValue(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id1)),
                    UnorderedElementsAre(
                        DbPair("person_id", StringFromInt(id1)),
                        DbPair("email", "person1@hotmail.com"),
                        DbPair("first_name", "first1"),
                        DbPair("last_name", "last1"),
                        DbPair("password_hash", "hash1")));
                EXPECT_THAT(
                    LookupRowByValue(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id2)),
                    UnorderedElementsAre(
                        DbPair("person_id", StringFromInt(id2)),
                        DbPair("email", "person2@hotmail.com"),
                        DbPair("first_name", "first2"),
                        DbPair("last_name", "last2"),
                        DbPair("password_hash", "hash2")));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateLookupRowByValuesSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateLookupRowByValueSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "email1"},
                    {"person_id", "1"},
                };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateLookupRowByValuesSql(transaction, databaseHelper, "test_people", keyValueTable1, execParamsHelpers),
                    "SELECT person_id, email, first_name, last_name, password_hash FROM test_people WHERE email = $1 AND person_id = $2");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("email1", "1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, LookupRowByValuesBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("LookupRowByValueBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                KeyValueTable lookupTable1 = {
                    {"person_id", StringFromInt(id1)}
                };
                EXPECT_THAT(
                    LookupRowByValues(transaction, databaseHelper, "test_people", lookupTable1),
                    UnorderedElementsAre(
                        DbPair("person_id", StringFromInt(id1)),
                        DbPair("email", "person1@hotmail.com"),
                        DbPair("first_name", "first1"),
                        DbPair("last_name", "last1"),
                        DbPair("password_hash", "hash1")));
                KeyValueTable lookupTable2 = {
                    {"person_id", StringFromInt(id2)},
                    {"email", "person2@hotmail.com"}
                };
                EXPECT_THAT(
                    LookupRowByValues(transaction, databaseHelper, "test_people", lookupTable2),
                    UnorderedElementsAre(
                        DbPair("person_id", StringFromInt(id2)),
                        DbPair("email", "person2@hotmail.com"),
                        DbPair("first_name", "first2"),
                        DbPair("last_name", "last2"),
                        DbPair("password_hash", "hash2")));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateUpdateRowSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateUpdateRowSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };

                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateUpdateRowSql(
                        transaction, databaseHelper, "test_people", "person_id", "1", keyValueTable1, StringSet(), execParamsHelpers),
                    "UPDATE test_people SET (email, first_name, last_name, password_hash) = ROW($2, $3, $4, $5) WHERE person_id = $1");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("1", "person1@hotmail.com", "first1", "last1", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateUpdateRowSqlSqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateUpdateRowSqlSqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "now()"},
                    {"last_name", "check"},
                    {"password_hash", "hash1"} };

                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateUpdateRowSql(
                        transaction, databaseHelper, "test_people", "person_id", "1", keyValueTable1, { "floof", "now()", "check" }, execParamsHelpers),
                    "UPDATE test_people SET (email, first_name, last_name, password_hash) = ROW($2, now(), check, $3) WHERE person_id = $1");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("1", "person1@hotmail.com", "hash1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GetTableRowsBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetTableRowsBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                std::string id1Str = StringFromInt(id1);
                std::string id2Str = StringFromInt(id2);

                keyValueTable1["person_id"] = id1Str;
                keyValueTable2["person_id"] = id2Str;
                KeyValueTableArray keyValueTableArrayComp = { keyValueTable1, keyValueTable2 };

                KeyValueTableArray keyValueTableArray = GetTableRows(transaction, databaseHelper, "test_people");

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(keyValueTableArrayComp, keyValueTableArray));

                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateGetRowsByColumnSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateGetRowsByColumnSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByColumnSql(transaction, databaseHelper, "test_people", "person_id", true, 10, 2),
                    "SELECT person_id, email, first_name, last_name, password_hash FROM test_people ORDER BY person_id ASC LIMIT $1 OFFSET $2");
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByColumnSql(transaction, databaseHelper, "test_people", "person_id", false, 5, 0),
                    "SELECT person_id, email, first_name, last_name, password_hash FROM test_people ORDER BY person_id DESC LIMIT $1 OFFSET $2");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByColumnBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateGetRowsByColumnSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "dfirst1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "cfirst2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };
                KeyValueTable keyValueTable3 = {
                    {"email", "person3@hotmail.com"},
                    {"first_name", "bfirst3"},
                    {"last_name", "last3"},
                    {"password_hash", "hash3"} };
                KeyValueTable keyValueTable4 = {
                    {"email", "person4@hotmail.com"},
                    {"first_name", "afirst4"},
                    {"last_name", "last4"},
                    {"password_hash", "hash4"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);
                int id3 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable3);
                int id4 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable4);

                std::string id1Str = StringFromInt(id1);
                std::string id2Str = StringFromInt(id2);
                std::string id3Str = StringFromInt(id3);
                std::string id4Str = StringFromInt(id4);

                keyValueTable1["person_id"] = id1Str;
                keyValueTable2["person_id"] = id2Str;
                keyValueTable3["person_id"] = id3Str;
                keyValueTable4["person_id"] = id4Str;

                KeyValueTableArray keyValueTableArray = GetRowsByColumn(transaction, databaseHelper, "test_people", "first_name", false, 2, 0);

                KeyValueTableArray keyValueTableArrayComp = { keyValueTable1, keyValueTable2 };

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(keyValueTableArrayComp, keyValueTableArray));

                keyValueTableArray = GetRowsByColumn(transaction, databaseHelper, "test_people", "first_name", false, 2, 1);

                keyValueTableArrayComp = { keyValueTable3, keyValueTable4 };

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(keyValueTableArrayComp, keyValueTableArray));

                });
        }

        TEST(DatabaseCrudHelpersTest, GetTableRowCountBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetTableRowCountBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                // Empty table should have count 0
                EXPECT_EQ(GetTableRowCount(transaction, databaseHelper, "test_people"), 0);

                // Add some rows
                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };
                KeyValueTable keyValueTable3 = {
                    {"email", "person3@hotmail.com"},
                    {"first_name", "first3"},
                    {"last_name", "last3"},
                    {"password_hash", "hash3"} };

                AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                EXPECT_EQ(GetTableRowCount(transaction, databaseHelper, "test_people"), 1);

                AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);
                EXPECT_EQ(GetTableRowCount(transaction, databaseHelper, "test_people"), 2);

                AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable3);
                EXPECT_EQ(GetTableRowCount(transaction, databaseHelper, "test_people"), 3);

                // Delete a row and verify count decreases
                DeleteRow(transaction, databaseHelper, "test_people", "email", "person2@hotmail.com");
                EXPECT_EQ(GetTableRowCount(transaction, databaseHelper, "test_people"), 2);
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateGetRowSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateGetRowSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                StringArray columnNames;
                // The expected SQL format is:
                // SELECT <columns> FROM <table> WHERE <column> = <value>
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowSql(transaction, databaseHelper, "test_people", "person_id", "1", columnNames),
                    "SELECT person_id, email, first_name, last_name, password_hash FROM test_people WHERE person_id = $1");
                EXPECT_THAT(columnNames, ElementsAre("person_id", "email", "first_name", "last_name", "password_hash"));
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                std::string id1Str = StringFromInt(id1);
                std::string id2Str = StringFromInt(id2);

                KeyValueTable keyValueTable = GetRow(transaction, databaseHelper, "test_people", "person_id", id1Str);

                keyValueTable1["person_id"] = id1Str;
                ASSERT_TRUE(CompareKeyValueTablesMinusColumns(keyValueTable1, keyValueTable));

                // Also test for the second row
                keyValueTable = GetRow(transaction, databaseHelper, "test_people", "person_id", id2Str);
                keyValueTable2["person_id"] = id2Str;
                ASSERT_TRUE(CompareKeyValueTablesMinusColumns(keyValueTable2, keyValueTable));
                });
        }

        TEST(DatabaseCrudHelpersTest, UpdateRowBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateRowBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                KeyValueTable keyValueTable3 = {
                    {"email", "person3@hotmail.com"},
                    {"first_name", "first3"} };
                KeyValueTable keyValueTable4 = {
                    {"last_name", "last4"},
                    {"password_hash", "hash4"} };

                UpdateRow(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id1), keyValueTable3);
                UpdateRow(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id2), keyValueTable4);

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id1), "person3@hotmail.com", "first3", "last1", "hash1"),
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last4", "hash4")
                    ));
                });
        }

        TEST(DatabaseCrudHelpersTest, UpdateRowSqlKeywords) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("UpdateRowSqlKeywords", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTableWithTimestamp(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"updated_at", "now()"} };

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1, { "now()" });

                StringTable results = transaction.RunSqlStatementReturningStringTable(select_statement);
                ASSERT_EQ(results.size(), 1);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(id1, 1);
                ASSERT_EQ(results[0][0], "1");
                ASSERT_EQ(results[0][1], "person1@hotmail.com");
                int64_t timestampFirst = DateTimeUtil::StringToEpochMillis(results[0][2]);

                KeyValueTable keyValueTableUpdate = {
                    {"updated_at", "now() + interval '1 hour'"} };

                UpdateRow(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id1), keyValueTableUpdate, { "now() + interval '1 hour'" });

                results = transaction.RunSqlStatementReturningStringTable(select_statement);
                ASSERT_EQ(results.size(), 1);
                ASSERT_EQ(results[0].size(), 3);
                ASSERT_EQ(id1, 1);
                ASSERT_EQ(results[0][0], "1");
                ASSERT_EQ(results[0][1], "person1@hotmail.com");
                int64_t timestampSecond = DateTimeUtil::StringToEpochMillis(results[0][2]);

                int64_t diff = timestampSecond - timestampFirst;
                ASSERT_TRUE(timestampSecond > timestampFirst);
                ASSERT_TRUE(diff >= 3560000 && diff < 3660000); // Allow for some leeway in timing
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateDeleteRowSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateDeleteRowSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                EXPECT_EQ(
                    PrivateSql::GenerateDeleteRowSql(
                        transaction, databaseHelper, "test_people", "person_id", "1"),
                    "DELETE FROM test_people WHERE person_id = $1");
                });
        }

        TEST(DatabaseCrudHelpersTest, DeleteRowBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteRowBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id1), "person1@hotmail.com", "first1", "last1", "hash1"),
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last2", "hash2")
                    ));

                DeleteRow(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id1));

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last2", "hash2")
                    ));

                DeleteRow(transaction, databaseHelper, "test_people", "person_id", StringFromInt(id2));

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre());
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateDeleteRowKeyValueTableSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateDeleteRowKeyValueTableSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable = { {"person_id", "1"}, {"last_name", "last1"} };
                ExecParamsHelper execParamsHelpers;
                EXPECT_EQ(
                    PrivateSql::GenerateDeleteRowSql(
                        transaction, databaseHelper, "test_people", keyValueTable, execParamsHelpers),
                    "DELETE FROM test_people WHERE last_name = $1 AND person_id = $2");
                EXPECT_THAT(execParamsHelpers.GetParamStringArray(), ElementsAre("last1", "1"));
                });
        }

        TEST(DatabaseCrudHelpersTest, DeleteRowKeyValueTableBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteRowKeyValueTableBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                KeyValueTable keyValueTable1 = {
                    {"email", "person1@hotmail.com"},
                    {"first_name", "first1"},
                    {"last_name", "last1"},
                    {"password_hash", "hash1"} };
                KeyValueTable keyValueTable2 = {
                    {"email", "person2@hotmail.com"},
                    {"first_name", "first2"},
                    {"last_name", "last2"},
                    {"password_hash", "hash2"} };

                int id1 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable1);
                int id2 = AddRowToTableFetchIntPrimaryKey(transaction, databaseHelper, "test_people", keyValueTable2);

                DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                    DbMeta::ListColumns(transaction, "test_people");
                std::string select_statement =
                    DbUtil::GenerateSelectStatementFromTableAndInfo(
                        "test_people", databaseColumnInfoArray);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id1), "person1@hotmail.com", "first1", "last1", "hash1"),
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last2", "hash2")
                    ));

                KeyValueTable keyValueTable = { {"person_id", StringFromInt(id1)}, {"last_name", "last1"} };
                DeleteRow(transaction, databaseHelper, "test_people", keyValueTable);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre(
                        ElementsAre(StringFromInt(id2), "person2@hotmail.com", "first2", "last2", "hash2")
                    ));

                keyValueTable = { {"person_id", StringFromInt(id2)}, {"last_name", "last2"} };
                DeleteRow(transaction, databaseHelper, "test_people", keyValueTable);

                EXPECT_THAT(
                    transaction.RunSqlStatementReturningStringTable(select_statement),
                    UnorderedElementsAre());
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesSingleFilter) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesSingleFilter", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");
                int personId2 = AddPerson(transaction, databaseHelper, "p2@test.com", "Bob", "Jones", "hash2");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "widget", 100);
                AddOrder(personId1, "gadget", 200);
                AddOrder(personId2, "doohickey", 300);

                // Filter to person1's test_orders only
                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };
                int64_t totalCount = 0;
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", filter, true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 2);
                EXPECT_EQ(totalCount, 2);
                // Sorted by item ASC: gadget, widget
                EXPECT_EQ(results[0]["item"], "gadget");
                EXPECT_EQ(results[1]["item"], "widget");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesMultipleFilters) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesMultipleFilters", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "widget", 100);
                AddOrder(personId1, "gadget", 200);
                AddOrder(personId1, "widget", 300);

                // Filter by person AND item
                KeyValueTable filter = {
                    {"parent_person_id", StringFromInt(personId1)},
                    {"item", "widget"} };
                int64_t totalCount = 0;
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "amount", filter, true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 2);
                EXPECT_EQ(totalCount, 2);
                // Sorted by amount ASC: 100, 300
                EXPECT_EQ(results[0]["amount"], "100");
                EXPECT_EQ(results[1]["amount"], "300");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesPagination) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesPagination", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "a_item", 100);
                AddOrder(personId1, "b_item", 200);
                AddOrder(personId1, "c_item", 300);
                AddOrder(personId1, "d_item", 400);

                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };

                // Page 0, size 2
                int64_t totalCount = 0;
                KeyValueTableArray page0 = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", filter, true, 2, 0, &totalCount);
                ASSERT_EQ(page0.size(), 2);
                EXPECT_EQ(totalCount, 4);
                EXPECT_EQ(page0[0]["item"], "a_item");
                EXPECT_EQ(page0[1]["item"], "b_item");

                // Page 1, size 2
                KeyValueTableArray page1 = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", filter, true, 2, 1, &totalCount);
                ASSERT_EQ(page1.size(), 2);
                EXPECT_EQ(totalCount, 4);
                EXPECT_EQ(page1[0]["item"], "c_item");
                EXPECT_EQ(page1[1]["item"], "d_item");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesSortDescending) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesSortDescending", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "alpha", 100);
                AddOrder(personId1, "beta", 200);
                AddOrder(personId1, "gamma", 300);

                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", filter, false, 10, 0);

                ASSERT_EQ(results.size(), 3);
                // Sorted by item DESC: gamma, beta, alpha
                EXPECT_EQ(results[0]["item"], "gamma");
                EXPECT_EQ(results[1]["item"], "beta");
                EXPECT_EQ(results[2]["item"], "alpha");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesEmptyFilters) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesEmptyFilters", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper, "p1@test.com", "Charlie", "Smith", "hash1");
                AddPerson(transaction, databaseHelper, "p2@test.com", "Alice", "Jones", "hash2");
                AddPerson(transaction, databaseHelper, "p3@test.com", "Bob", "Brown", "hash3");

                // Empty filter = all rows (like GetRowsByColumn)
                KeyValueTable emptyFilter;
                int64_t totalCount = 0;
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_people", "first_name", emptyFilter, true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 3);
                EXPECT_EQ(totalCount, 3);
                // Sorted by first_name ASC: Alice, Bob, Charlie
                EXPECT_EQ(results[0]["first_name"], "Alice");
                EXPECT_EQ(results[1]["first_name"], "Bob");
                EXPECT_EQ(results[2]["first_name"], "Charlie");
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesNullTotalCount) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesNullTotalCount", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");
                AddPerson(transaction, databaseHelper, "p2@test.com", "Bob", "Jones", "hash2");

                // nullptr totalCount should work fine
                KeyValueTable emptyFilter;
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_people", "first_name", emptyFilter, true, 10, 0, nullptr);

                ASSERT_EQ(results.size(), 2);
                EXPECT_EQ(results[0]["first_name"], "Alice");
                EXPECT_EQ(results[1]["first_name"], "Bob");

                // Verify _total_count column was stripped from results
                EXPECT_TRUE(results[0].find("_total_count") == results[0].end());
                });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesTotalCountReflectsFilter) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesTotalCountReflectsFilter", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");
                int personId2 = AddPerson(transaction, databaseHelper, "p2@test.com", "Bob", "Jones", "hash2");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "a", 100);
                AddOrder(personId1, "b", 200);
                AddOrder(personId1, "c", 300);
                AddOrder(personId2, "d", 400);
                AddOrder(personId2, "e", 500);

                // Total table has 5 rows, but filter to person1 gives 3
                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };
                int64_t totalCount = 0;
                KeyValueTableArray results = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", filter, true, 2, 0, &totalCount);

                ASSERT_EQ(results.size(), 2); // page size 2
                EXPECT_EQ(totalCount, 3);     // 3 filtered rows, not 5 total

                // Empty filter gives total count of 5
                KeyValueTable emptyFilter;
                results = GetRowsByValues(
                    transaction, databaseHelper, "test_orders", "item", emptyFilter, true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 5);
                EXPECT_EQ(totalCount, 5);
                });
        }

        TEST(DatabaseCrudHelpersTest, GenerateGetRowsByValuesSqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateGetRowsByValuesSqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                // No filters
                ExecParamsHelper execParams1;
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByValuesSql(
                        transaction, databaseHelper, "test_people", "person_id", {}, true, 10, 0, execParams1),
                    "SELECT person_id, email, first_name, last_name, password_hash, COUNT(*) OVER() AS _total_count"
                    " FROM test_people ORDER BY person_id ASC LIMIT NULLIF($1, 0) OFFSET $2");
                EXPECT_THAT(execParams1.GetParamStringArray(), ElementsAre("10", "0"));

                // With filter
                ExecParamsHelper execParams2;
                KeyValueTable filter = { {"first_name", "Alice"} };
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByValuesSql(
                        transaction, databaseHelper, "test_people", "email", filter, false, 5, 2, execParams2),
                    "SELECT person_id, email, first_name, last_name, password_hash, COUNT(*) OVER() AS _total_count"
                    " FROM test_people WHERE first_name = $1 ORDER BY email DESC LIMIT NULLIF($2, 0) OFFSET $3");
                EXPECT_THAT(execParams2.GetParamStringArray(), ElementsAre("Alice", "5", "10"));
                });
        }

        TEST(DatabaseCrudHelpersTest, ParseTemplateColumnsSingleColumn) {
            StringArray columns = ParseTemplateColumns("{name}");
            EXPECT_THAT(columns, ElementsAre("name"));
        }

        TEST(DatabaseCrudHelpersTest, ParseTemplateColumnsMultipleColumns) {
            StringArray columns = ParseTemplateColumns(
                "{first_name} {last_name} - {email}");
            EXPECT_THAT(columns, ElementsAre(
                "first_name", "last_name", "email"));
        }

        TEST(DatabaseCrudHelpersTest, ParseTemplateColumnsEmptyTemplate) {
            StringArray columns = ParseTemplateColumns("");
            EXPECT_TRUE(columns.empty());
        }

        TEST(DatabaseCrudHelpersTest, ParseTemplateColumnsNoPlaceholders) {
            StringArray columns = ParseTemplateColumns("static text only");
            EXPECT_TRUE(columns.empty());
        }

        TEST(DatabaseCrudHelpersTest, SearchRowsByILikeBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SearchRowsByILikeBasic",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash1");
                AddPerson(transaction, databaseHelper,
                    "bob@test.com", "Bob", "Jones", "hash2");
                AddPerson(transaction, databaseHelper,
                    "charlie@test.com", "Charlie", "Smith", "hash3");

                // Search by first_name for "alice"
                StringArray searchColumns = {"first_name"};
                int64_t totalCount = 0;
                KeyValueTableArray results = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "person_id",
                    searchColumns, "alice", true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 1);
                EXPECT_EQ(totalCount, 1);
                EXPECT_EQ(results[0]["first_name"], "Alice");
            });
        }

        TEST(DatabaseCrudHelpersTest, SearchRowsByILikeCaseInsensitive) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SearchRowsByILikeCaseInsensitive",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash1");

                // Search with uppercase
                StringArray searchColumns = {"first_name"};
                int64_t totalCount = 0;
                KeyValueTableArray results = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "person_id",
                    searchColumns, "ALICE", true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 1);
                EXPECT_EQ(totalCount, 1);
                EXPECT_EQ(results[0]["first_name"], "Alice");
            });
        }

        TEST(DatabaseCrudHelpersTest, SearchRowsByILikeMultipleColumns) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SearchRowsByILikeMultipleColumns",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash1");
                AddPerson(transaction, databaseHelper,
                    "bob@test.com", "Bob", "Jones", "hash2");

                // Search across first_name and last_name for "smith"
                StringArray searchColumns = {"first_name", "last_name"};
                int64_t totalCount = 0;
                KeyValueTableArray results = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "person_id",
                    searchColumns, "smith", true, 10, 0, &totalCount);

                ASSERT_EQ(results.size(), 1);
                EXPECT_EQ(totalCount, 1);
                EXPECT_EQ(results[0]["first_name"], "Alice");
            });
        }

        TEST(DatabaseCrudHelpersTest, SearchRowsByILikePagination) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SearchRowsByILikePagination",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper,
                    "a@test.com", "Alice", "Smith", "hash1");
                AddPerson(transaction, databaseHelper,
                    "b@test.com", "Bob", "Smith", "hash2");
                AddPerson(transaction, databaseHelper,
                    "c@test.com", "Charlie", "Smith", "hash3");

                // Search for "smith" with page size 2
                StringArray searchColumns = {"last_name"};
                int64_t totalCount = 0;
                KeyValueTableArray page0 = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "first_name",
                    searchColumns, "smith", true, 2, 0, &totalCount);

                ASSERT_EQ(page0.size(), 2);
                EXPECT_EQ(totalCount, 3);

                KeyValueTableArray page1 = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "first_name",
                    searchColumns, "smith", true, 2, 1, &totalCount);

                ASSERT_EQ(page1.size(), 1);
                EXPECT_EQ(totalCount, 3);
            });
        }

        TEST(DatabaseCrudHelpersTest, SearchRowsByILikeNoMatch) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SearchRowsByILikeNoMatch",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper =
                    testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash1");

                StringArray searchColumns = {"first_name"};
                int64_t totalCount = 0;
                KeyValueTableArray results = SearchRowsByILike(
                    transaction, databaseHelper, "test_people", "person_id",
                    searchColumns, "zzz_no_match", true, 10, 0, &totalCount);

                EXPECT_EQ(results.size(), 0);
                EXPECT_EQ(totalCount, 0);
            });
        }

        TEST(DatabaseCrudHelpersTest, ResolveDisplayValuesSingleRow) {
            KeyValueTableArray rows = {
                { {"person_id", "42"}, {"first_name", "Alice"}, {"last_name", "Smith"} }
            };
            KeyValueTable result = ResolveDisplayValues(rows, "person_id", "{first_name} {last_name}");
            EXPECT_THAT(result, UnorderedElementsAre(Pair("42", "Alice Smith")));
        }

        TEST(DatabaseCrudHelpersTest, ResolveDisplayValuesMultipleRows) {
            KeyValueTableArray rows = {
                { {"id", "1"}, {"name", "Admin"} },
                { {"id", "2"}, {"name", "User"} },
                { {"id", "3"}, {"name", "Guest"} }
            };
            KeyValueTable result = ResolveDisplayValues(rows, "id", "{name}");
            EXPECT_THAT(result, UnorderedElementsAre(
                Pair("1", "Admin"),
                Pair("2", "User"),
                Pair("3", "Guest")));
        }

        TEST(DatabaseCrudHelpersTest, ResolveDisplayValuesMultiplePlaceholders) {
            KeyValueTableArray rows = {
                { {"person_id", "7"}, {"first_name", "Bob"}, {"last_name", "Jones"}, {"email", "bob@test.com"} }
            };
            KeyValueTable result = ResolveDisplayValues(rows, "person_id", "{first_name} {last_name} - {email}");
            EXPECT_THAT(result, UnorderedElementsAre(Pair("7", "Bob Jones - bob@test.com")));
        }

        TEST(DatabaseCrudHelpersTest, ResolveDisplayValuesEmptyTemplateReturnsPrimaryKey) {
            KeyValueTableArray rows = {
                { {"id", "10"}, {"name", "Something"} },
                { {"id", "20"}, {"name", "Else"} }
            };
            KeyValueTable result = ResolveDisplayValues(rows, "id", "");
            EXPECT_THAT(result, UnorderedElementsAre(
                Pair("10", "10"),
                Pair("20", "20")));
        }

        TEST(DatabaseCrudHelpersTest, LookupRowsByPrimaryKeyValuesBasic) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("LookupRowsByPrimaryKeyValuesBasic",
                [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDb);
                auto databaseHelper = testDb.GetDatabaseHelper();
                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash");
                int bobId = AddPerson(transaction, databaseHelper,
                    "bob@test.com", "Bob", "Jones", "hash");
                int charlieId = AddPerson(transaction, databaseHelper,
                    "charlie@test.com", "Charlie", "Brown", "hash");

                StringArray values = {
                    std::to_string(bobId), std::to_string(charlieId) };
                KeyValueTableArray results = LookupRowsByPrimaryKeyValues(
                    transaction, databaseHelper, "test_people", "person_id", values);

                ASSERT_EQ(results.size(), 2);
                StringArray emails;
                for (const auto& row : results) {
                    emails.push_back(row.at("email"));
                }
                EXPECT_THAT(emails, UnorderedElementsAre(
                    "bob@test.com", "charlie@test.com"));
            });
        }

        TEST(DatabaseCrudHelpersTest, LookupRowsByPrimaryKeyValuesEmpty) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("LookupRowsByPrimaryKeyValuesEmpty",
                [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDb);
                auto databaseHelper = testDb.GetDatabaseHelper();
                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash");

                StringArray values = {};
                KeyValueTableArray results = LookupRowsByPrimaryKeyValues(
                    transaction, databaseHelper, "test_people", "person_id", values);

                EXPECT_EQ(results.size(), 0);
            });
        }

        TEST(DatabaseCrudHelpersTest, LookupRowsByPrimaryKeyValuesNoMatch) {
            TestDatabaseUtil testDb;
            testDb.RunInTransaction("LookupRowsByPrimaryKeyValuesNoMatch",
                [&](Transaction& transaction) {
                MakePeopleTable(transaction, testDb);
                auto databaseHelper = testDb.GetDatabaseHelper();
                AddPerson(transaction, databaseHelper,
                    "alice@test.com", "Alice", "Smith", "hash");

                StringArray values = { "99999", "88888" };
                KeyValueTableArray results = LookupRowsByPrimaryKeyValues(
                    transaction, databaseHelper, "test_people", "person_id", values);

                EXPECT_EQ(results.size(), 0);
            });
        }

        // --- GetRowsByValuesWithOrderBy ---

        TEST(DatabaseCrudHelpersTest, GenerateGetRowsByValuesWithOrderBySqlBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GenerateGetRowsByValuesWithOrderBySqlBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                // Single filter
                ExecParamsHelper execParams1;
                KeyValueTable filter1 = { {"parent_person_id", "5"} };
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByValuesWithOrderBySql(
                        transaction, databaseHelper, "test_orders", filter1, "item", true, execParams1),
                    "SELECT order_id, parent_person_id, item, amount"
                    " FROM test_orders WHERE parent_person_id = $1 ORDER BY item ASC");
                EXPECT_THAT(execParams1.GetParamStringArray(), ElementsAre("5"));

                // Multiple filters, DESC
                ExecParamsHelper execParams2;
                KeyValueTable filter2 = { {"parent_person_id", "5"}, {"item", "widget"} };
                EXPECT_EQ(
                    PrivateSql::GenerateGetRowsByValuesWithOrderBySql(
                        transaction, databaseHelper, "test_orders", filter2, "amount", false, execParams2),
                    "SELECT order_id, parent_person_id, item, amount"
                    " FROM test_orders WHERE item = $1 AND parent_person_id = $2 ORDER BY amount DESC");
                EXPECT_THAT(execParams2.GetParamStringArray(), ElementsAre("widget", "5"));
            });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesWithOrderBySingleFilter) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesWithOrderBySingleFilter", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");
                int personId2 = AddPerson(transaction, databaseHelper, "p2@test.com", "Bob", "Jones", "hash2");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "widget", 100);
                AddOrder(personId1, "gadget", 200);
                AddOrder(personId2, "doohickey", 300);

                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };
                KeyValueTableArray results = GetRowsByValuesWithOrderBy(
                    transaction, databaseHelper, "test_orders", filter, "item", true);

                ASSERT_EQ(results.size(), 2);
                EXPECT_EQ(results[0]["item"], "gadget");
                EXPECT_EQ(results[1]["item"], "widget");
            });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesWithOrderByMultipleFilters) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesWithOrderByMultipleFilters", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "widget", 300);
                AddOrder(personId1, "gadget", 200);
                AddOrder(personId1, "widget", 100);

                KeyValueTable filter = {
                    {"parent_person_id", StringFromInt(personId1)},
                    {"item", "widget"} };
                KeyValueTableArray results = GetRowsByValuesWithOrderBy(
                    transaction, databaseHelper, "test_orders", filter, "amount", true);

                ASSERT_EQ(results.size(), 2);
                EXPECT_EQ(results[0]["amount"], "100");
                EXPECT_EQ(results[1]["amount"], "300");
            });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesWithOrderByDescending) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesWithOrderByDescending", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                int personId1 = AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                auto AddOrder = [&](int personId, std::string_view item, int amount) {
                    return AddRowToTableFetchIntPrimaryKey(
                        transaction, databaseHelper, "test_orders",
                        { DbPair("parent_person_id", StringFromInt(personId)),
                          DbPair("item", item),
                          DbPair("amount", std::to_string(amount)) });
                };

                AddOrder(personId1, "a_item", 100);
                AddOrder(personId1, "b_item", 200);
                AddOrder(personId1, "c_item", 300);

                KeyValueTable filter = { {"parent_person_id", StringFromInt(personId1)} };
                KeyValueTableArray results = GetRowsByValuesWithOrderBy(
                    transaction, databaseHelper, "test_orders", filter, "item", false);

                ASSERT_EQ(results.size(), 3);
                EXPECT_EQ(results[0]["item"], "c_item");
                EXPECT_EQ(results[1]["item"], "b_item");
                EXPECT_EQ(results[2]["item"], "a_item");
            });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesWithOrderByNoResults) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesWithOrderByNoResults", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);
                MakeOrdersTable(transaction, testDatabaseUtil);

                AddPerson(transaction, databaseHelper, "p1@test.com", "Alice", "Smith", "hash1");

                KeyValueTable filter = { {"parent_person_id", "999999"} };
                KeyValueTableArray results = GetRowsByValuesWithOrderBy(
                    transaction, databaseHelper, "test_orders", filter, "item", true);

                EXPECT_TRUE(results.empty());
            });
        }

        TEST(DatabaseCrudHelpersTest, GetRowsByValuesWithOrderByEmptyFiltersThrows) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetRowsByValuesWithOrderByEmptyFiltersThrows", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                MakePeopleTable(transaction, testDatabaseUtil);

                ExecParamsHelper execParams;
                EXPECT_THROW(
                    PrivateSql::GenerateGetRowsByValuesWithOrderBySql(
                        transaction, databaseHelper, "test_people", {}, "person_id", true, execParams),
                    std::invalid_argument);
            });
        }

    } // namespace {
}  // namespace DbCrud
