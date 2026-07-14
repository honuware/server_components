#include "table_matcher.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "database_test_helper.h"

TEST(TableWatcherTest, PostGresValueBasicConstructor) {
    PostGresValue pgv;
    ASSERT_EQ(pgv.GetType(), PostGresValue::PV_TYPE_NULL);
    ASSERT_TRUE(pgv.IsNull());
}

TEST(TableWatcherTest, PostGresValueCopyConstructor) {
    PostGresValue pgv1(5);
    ASSERT_EQ(pgv1.GetType(), PostGresValue::PV_TYPE_INT);
    PostGresValue pgv2(pgv1);
    ASSERT_EQ(pgv2.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgv1.GetInt(), 5);
    ASSERT_EQ(pgv2.GetInt(), 5);
}

TEST(TableWatcherTest, PostGresValueAssignment) {
    PostGresValue pgv1(5), pgv2("test");
    ASSERT_EQ(pgv1.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgv1.GetInt(), 5);
    ASSERT_EQ(pgv2.GetType(), PostGresValue::PV_TYPE_STRING);
    ASSERT_EQ(pgv2.GetString(), "test");
    pgv2 = pgv1;
    ASSERT_EQ(pgv1.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgv1.GetInt(), 5);
    ASSERT_EQ(pgv2.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgv2.GetInt(), 5);
}

TEST(TableWatcherTest, PostGresValueValueConstructors) {
    PostGresValue pgvInt(5);
    PostGresValue pgvUint(static_cast<unsigned int>(3));
    PostGresValue pgvFloat(static_cast<float>(3.14));
    PostGresValue pgvDouble(static_cast<double>(6.7));
    PostGresValue pgvBool(true);
    PostGresValue pgvCharStar("test");
    PostGresValue pgvString(std::string("test_string"));
    PostGresValue pgvStringView(std::string_view("string_view"));

    ASSERT_EQ(pgvInt.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgvUint.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgvFloat.GetType(), PostGresValue::PV_TYPE_DOUBLE);
    ASSERT_EQ(pgvDouble.GetType(), PostGresValue::PV_TYPE_DOUBLE);
    ASSERT_EQ(pgvBool.GetType(), PostGresValue::PV_TYPE_BOOL);
    ASSERT_EQ(pgvCharStar.GetType(), PostGresValue::PV_TYPE_STRING);
    ASSERT_EQ(pgvString.GetType(), PostGresValue::PV_TYPE_STRING);
    ASSERT_EQ(pgvStringView.GetType(), PostGresValue::PV_TYPE_STRING);

    ASSERT_EQ(pgvInt.GetInt(), 5);
    ASSERT_EQ(pgvUint.GetInt(), 3);
    // Skip double comparison since it is imprecise
    // ASSERT_DOUBLE_EQ(pgvFloat.GetDouble(), 3.14);
    // ASSERT_DOUBLE_EQ(pgvDouble.GetDouble(), 6.7);
    ASSERT_EQ(pgvBool.GetBool(), true);
    ASSERT_EQ(pgvCharStar.GetString(), "test");
    ASSERT_EQ(pgvString.GetString(), "test_string");
    ASSERT_EQ(pgvStringView.GetString(), "string_view");
}

TEST(TableWatcherTest, PostGresValueSetters) {
    PostGresValue pgvInt;
    PostGresValue pgvUint;
    PostGresValue pgvFloat;
    PostGresValue pgvDouble;
    PostGresValue pgvBool;
    PostGresValue pgvCharStar;
    PostGresValue pgvString;
    PostGresValue pgvStringView;

    pgvInt.Set(5);
    pgvUint.Set(static_cast<unsigned int>(3));
    pgvFloat.Set(static_cast<float>(3.14));
    pgvDouble.Set(static_cast<double>(6.7));
    pgvBool.Set(true);
    pgvCharStar.Set("test");
    pgvString.Set(std::string("test_string"));
    pgvStringView.Set(std::string_view("string_view"));

    ASSERT_EQ(pgvInt.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgvUint.GetType(), PostGresValue::PV_TYPE_INT);
    ASSERT_EQ(pgvFloat.GetType(), PostGresValue::PV_TYPE_DOUBLE);
    ASSERT_EQ(pgvDouble.GetType(), PostGresValue::PV_TYPE_DOUBLE);
    ASSERT_EQ(pgvBool.GetType(), PostGresValue::PV_TYPE_BOOL);
    ASSERT_EQ(pgvCharStar.GetType(), PostGresValue::PV_TYPE_STRING);
    ASSERT_EQ(pgvString.GetType(), PostGresValue::PV_TYPE_STRING);
    ASSERT_EQ(pgvStringView.GetType(), PostGresValue::PV_TYPE_STRING);

    ASSERT_EQ(pgvInt.GetInt(), 5);
    ASSERT_EQ(pgvUint.GetInt(), 3);
    // Skip double comparison since it is imprecise
    // ASSERT_DOUBLE_EQ(pgvFloat.GetDouble(), 3.14);
    // ASSERT_DOUBLE_EQ(pgvDouble.GetDouble(), 6.7);
    ASSERT_EQ(pgvBool.GetBool(), true);
    ASSERT_EQ(pgvCharStar.GetString(), "test");
    ASSERT_EQ(pgvString.GetString(), "test_string");
    ASSERT_EQ(pgvStringView.GetString(), "string_view");
}

TEST(TableWatcherTest, PostGresValueEquality) {
    PostGresValue pgvInt(5);
    PostGresValue pgvUint(static_cast<unsigned int>(3));
    PostGresValue pgvFloat(static_cast<float>(3.14));
    PostGresValue pgvDouble(static_cast<double>(6.7));
    PostGresValue pgvBool(true);
    PostGresValue pgvCharStar("test");
    PostGresValue pgvString(std::string("test_string"));
    PostGresValue pgvStringView(std::string_view("string_view"));

    ASSERT_EQ(pgvInt, 5);
    ASSERT_EQ(pgvUint, 3);
    ASSERT_EQ(pgvBool, true);
    ASSERT_EQ(pgvCharStar, "test");
    ASSERT_EQ(pgvString, "test_string");
    ASSERT_EQ(pgvStringView, "string_view");
}

TEST(TableWatcherTest, PostGresValueIsMatch) {
    PostGresValue pgvInt(5);
    PostGresValue pgvUint(static_cast<unsigned int>(3));
    PostGresValue pgvFloat(static_cast<float>(3.14));
    PostGresValue pgvDouble(static_cast<double>(6.7));
    PostGresValue pgvBool(true);
    PostGresValue pgvCharStar("test");
    PostGresValue pgvString(std::string("test_string"));
    PostGresValue pgvStringView(std::string_view("string_view"));

    ASSERT_TRUE(pgvInt.IsMatch(5));
    ASSERT_TRUE(pgvUint.IsMatch(3));
    // Skip double comparison since it is imprecise
    // ASSERT_TRUE(pgvFloat.IsMatch(3.14));
    // ASSERT_TRUE(pgvDouble.IsMatch(6.7));
    ASSERT_TRUE(pgvBool.IsMatch(true));
    ASSERT_TRUE(pgvCharStar.IsMatch("test"));
    ASSERT_TRUE(pgvString.IsMatch("test_string"));
    ASSERT_TRUE(pgvStringView.IsMatch("string_view"));
}

TEST(TableWatcherTest, PostGresValueToString) {
    PostGresValue pgvInt(5);
    PostGresValue pgvUint(static_cast<unsigned int>(3));
    PostGresValue pgvFloat(static_cast<float>(3.14));
    PostGresValue pgvDouble(static_cast<double>(6.7));
    PostGresValue pgvBool(true);
    PostGresValue pgvCharStar("test");
    PostGresValue pgvString(std::string("test_string"));
    PostGresValue pgvStringView(std::string_view("string_view"));

    ASSERT_EQ(pgvInt.ToString(), "5");
    ASSERT_EQ(pgvUint.ToString(), "3");
    ASSERT_EQ(pgvBool.ToString(), "true");
    ASSERT_EQ(pgvCharStar.ToString(), "test");
    ASSERT_EQ(pgvString.ToString(), "test_string");
    ASSERT_EQ(pgvStringView.ToString(), "string_view");
}

TEST(TableWatcherTest, PostGresValueDescribe) {
    PostGresValue pgvInt(5);
    PostGresValue pgvUint(static_cast<unsigned int>(3));
    PostGresValue pgvFloat(static_cast<float>(3.14));
    PostGresValue pgvDouble(static_cast<double>(6.7));
    PostGresValue pgvBool(true);
    PostGresValue pgvCharStar("test");
    PostGresValue pgvString(std::string("test_string"));
    PostGresValue pgvStringView(std::string_view("string_view"));

    ASSERT_EQ(pgvInt.Describe(), "int(5)");
    ASSERT_EQ(pgvUint.Describe(), "int(3)");
    ASSERT_EQ(pgvBool.Describe(), "bool(true)");
    ASSERT_EQ(pgvCharStar.Describe(), "string(test)");
    ASSERT_EQ(pgvString.Describe(), "string(test_string)");
    ASSERT_EQ(pgvStringView.Describe(), "string(string_view)");
}

constexpr std::string_view kCreateTable = R"SQL_TEXT(
CREATE TABLE IF NOT EXISTS test_table (
  id SERIAL PRIMARY KEY,
  intVal INT,
  boolVal BOOL,
  stringVal VARCHAR
)
)SQL_TEXT";

constexpr std::string_view kAddItemSproc = R"SQL_TEXT(
CREATE PROCEDURE add_item(intValParam INT, boolValParam BOOL, stringValParam VARCHAR, INOUT id_arg int)
LANGUAGE plpgsql
AS $$
BEGIN
INSERT INTO test_table (intVal, boolVal, stringVal) VALUES (intValParam, boolValParam, stringValParam) RETURNING id INTO id_arg;
END;
$$
)SQL_TEXT";

constexpr std::string_view kGetItemsSproc = R"SQL_TEXT(
CREATE FUNCTION get_items()
RETURNS TABLE(id int, intVal INT, boolVal BOOL, stringVal VARCHAR)
LANGUAGE SQL
AS $$
SELECT id, intVal, boolVal, stringVal FROM test_table;
$$
)SQL_TEXT";

void CreateItemsTable(DatabaseHelper& databaseHelper) {
    pqxx::work trans(databaseHelper.GetConnection(), "Create test_table");
    trans.exec(kCreateTable);
    trans.commit();
}

void CreateAddItemProc(DatabaseHelper& databaseHelper) {
    pqxx::work trans(databaseHelper.GetConnection(), "Create add_item sproc");
    trans.exec(kAddItemSproc);
    trans.commit();
}

void CreateGetItemsProc(DatabaseHelper& databaseHelper) {
    pqxx::work trans(databaseHelper.GetConnection(), "Create get_items sproc");
    trans.exec(kGetItemsSproc);
    trans.commit();
}

int CallAddItem(DatabaseHelper& databaseHelper, int intVal, bool boolVal, std::string_view stringVal) {
    pqxx::work trans(databaseHelper.GetConnection(), "Add item");
    pqxx::row result = trans.exec_params1("CALL add_item($1, $2, $3, 0)", intVal, boolVal, stringVal);
    trans.commit();
    return result[0].as<int>();
}

pqxx::result CallGetItems(DatabaseHelper& databaseHelper) {
    pqxx::work trans(databaseHelper.GetConnection(), "Get classes");
    pqxx::result result = trans.exec("SELECT intVal, boolVal, stringVal FROM test_table");
    trans.commit();
    return result;
}

TEST(TableWatcherTest, TableIsBasic) {
    /*
    TODO(mason)- not sure if we need this class. If so, finish implementing the test.
    DatabaseHelper databaseHelper = MakeTestDatabaseHelper();
    CreateItemsTable(databaseHelper);
    CreateAddItemProc(databaseHelper);
    CreateGetItemsProc(databaseHelper);
    CallAddItem(databaseHelper, 1, true, "test1");
    pqxx::result res = CallGetItems(databaseHelper);
    EXPECT_THAT(res, TableIs({"intVal", "boolVal", "stringVal"}, {{1, true, "test1"}}));
    CallAddItem(databaseHelper, 2, false, "test2");
    res = CallGetItems(databaseHelper);
    EXPECT_THAT(res, TableIs({"intVal", "boolVal", "stringVal"}, {{1, true, "test1"}, {2, false, "test2"}}));
    */
}