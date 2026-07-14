#include "json_util.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace
{

    using ::testing::Contains;
    using ::testing::ElementsAreArray;
    using ::testing::UnorderedElementsAre;
    using ::testing::ElementsAre;
    using ::testing::Eq;

    TEST(JsonUtilTest, EqualJsonValuesIntValuePositive)
    {
        crow::json::wvalue value1(5);
        crow::json::wvalue value2(5);
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesIntValueNegative)
    {
        crow::json::wvalue value1(4);
        crow::json::wvalue value2(5);
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesIntAndStringValueNegative)
    {
        crow::json::wvalue value1(5);
        crow::json::wvalue value2("5");
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesTruePositive)
    {
        crow::json::wvalue value1(true);
        crow::json::wvalue value2(true);
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesBoolMixed)
    {
        crow::json::wvalue value1(true);
        crow::json::wvalue value2(false);
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesFalsePositive)
    {
        crow::json::wvalue value1(false);
        crow::json::wvalue value2(false);
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesNullPositive)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesNullMixed)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2(true);
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesObjectPositive)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1["key1"] = 5;
        value2["key1"] = 5;
        value1["key2"] = "data";
        value2["key2"] = "data";
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesObjectDifferentKeys)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1["key1"] = 5;
        value2["key3"] = 5;
        value1["key2"] = "data";
        value2["key2"] = "data";
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesObjectDifferentValues)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1["key1"] = 5;
        value2["key1"] = 5;
        value1["key2"] = "data";
        value2["key2"] = "data2";
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesObjectDifferentSizes)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1["key1"] = 5;
        value2["key1"] = 5;
        value1["key2"] = "data";
        value2["key2"] = "data2";
        value1["key3"] = 6;
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesListPositive)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1[0] = 5;
        value2[0] = 5;
        value1[1] = "data";
        value2[1] = "data";
        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesListDifferentValues)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1[0] = 5;
        value2[0] = 5;
        value1[1] = "data";
        value2[1] = "data2";
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesListDifferentSizes)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        value1[0] = 5;
        value2[0] = 5;
        value1[1] = "data";
        value2[1] = "data";
        value1[2] = 6;
        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, WvalueFromTextBasic)
    {
        crow::json::wvalue value1;
        value1[0] = 5;
        value1[1] = "data";
        constexpr std::string_view kJson = "[5, \"data\"]";

        ASSERT_TRUE(Json::EqualJsonValues(
            value1, Json::WvalueFromText(kJson)));
    }

    TEST(JsonUtilTest, EqualJsonValuesNestedPositive)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        crow::json::wvalue value1a;
        crow::json::wvalue value2a;
        crow::json::wvalue value1b;
        crow::json::wvalue value2b;
        crow::json::wvalue value1c;
        crow::json::wvalue value2c;
        value1a[0] = 5;
        value2a[0] = 5;
        value1a[1] = "data";
        value2a[1] = "data";
        value1c["key1"] = 15;
        value2c["key1"] = 15;
        value1b[0]["key1"] = 15;
        value2b[0]["key1"] = 15;
        value1b[0]["key2"] = "data1";
        value2b[0]["key2"] = "data1";
        value1["keya"] = std::move(value1a);
        value2["keya"] = std::move(value2a);
        value1["keyb"] = std::move(value1b);
        value2["keyb"] = std::move(value2b);
        value1["keyc"] = std::move(value1c);
        value2["keyc"] = std::move(value2c);

        ASSERT_TRUE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesNestedDifferentArray)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        crow::json::wvalue value1a;
        crow::json::wvalue value2a;
        crow::json::wvalue value1b;
        crow::json::wvalue value2b;
        crow::json::wvalue value1c;
        crow::json::wvalue value2c;
        value1a[0] = 5;
        value2a[0] = 6;
        value1a[1] = "data";
        value2a[1] = "data";
        value1c["key1"] = 15;
        value2c["key1"] = 15;
        value1b[0]["key1"] = 15;
        value2b[0]["key1"] = 15;
        value1b[0]["key2"] = "data1";
        value2b[0]["key2"] = "data1";
        value1["keya"] = std::move(value1a);
        value2["keya"] = std::move(value2a);
        value1["keyb"] = std::move(value1b);
        value2["keyb"] = std::move(value2b);
        value1["keyc"] = std::move(value1c);
        value2["keyc"] = std::move(value2c);

        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesNestedDifferentObject)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        crow::json::wvalue value1a;
        crow::json::wvalue value2a;
        crow::json::wvalue value1b;
        crow::json::wvalue value2b;
        crow::json::wvalue value1c;
        crow::json::wvalue value2c;
        value1a[0] = 5;
        value2a[0] = 5;
        value1a[1] = "data";
        value2a[1] = "data";
        value1c["key1"] = 15;
        value2c["key1"] = 20;
        value1b[0]["key1"] = 15;
        value2b[0]["key1"] = 15;
        value1b[0]["key2"] = "data1";
        value2b[0]["key2"] = "data1";
        value1["keya"] = std::move(value1a);
        value2["keya"] = std::move(value2a);
        value1["keyb"] = std::move(value1b);
        value2["keyb"] = std::move(value2b);
        value1["keyc"] = std::move(value1c);
        value2["keyc"] = std::move(value2c);

        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, EqualJsonValuesNestedExtraValue)
    {
        crow::json::wvalue value1;
        crow::json::wvalue value2;
        crow::json::wvalue value1a;
        crow::json::wvalue value2a;
        crow::json::wvalue value1b;
        crow::json::wvalue value2b;
        crow::json::wvalue value1c;
        crow::json::wvalue value2c;
        value1a[0] = 5;
        value2a[0] = 5;
        value1a[1] = "data";
        value2a[1] = "data";
        value1c["key1"] = 15;
        value2c["key1"] = 15;
        value1c["key2"] = 500;
        value1b[0]["key1"] = 15;
        value2b[0]["key1"] = 15;
        value1b[0]["key2"] = "data1";
        value2b[0]["key2"] = "data1";
        value1["keya"] = std::move(value1a);
        value2["keya"] = std::move(value2a);
        value1["keyb"] = std::move(value1b);
        value2["keyb"] = std::move(value2b);
        value1["keyc"] = std::move(value1c);
        value2["keyc"] = std::move(value2c);

        ASSERT_FALSE(Json::EqualJsonValues(value1, value2));
    }

    TEST(JsonUtilTest, JsonFromKeyValueTableBasic)
    {
        KeyValueTable table = { {"key1", "value1"}, {"key2", "value2"} };
        Json::Value value;
        value["key1"] = "value1";
        value["key2"] = "value2";

        ASSERT_EQ(value, Json::JsonFromKeyValueTable(table));
    }

    TEST(JsonUtilTest, JsonFromKeyValueTableArrayBasic)
    {
        KeyValueTableArray keyValueTableArray = {
            { {"key1", "value1"}, {"key2", "value2"} },
            { {"keyA", "valueA"}, {"keyB", "valueB"} }
        };
        Json::Value value, value1, value2;
        value1["key1"] = "value1";
        value1["key2"] = "value2";
        value2["keyA"] = "valueA";
        value2["keyB"] = "valueB";
        value.GetArray().push_back(value1);
        value.GetArray().push_back(value2);

        ASSERT_EQ(value, Json::JsonFromKeyValueTableArray(keyValueTableArray));
    }

    TEST(JsonUtilTest, KeyValueTableFromJsonBasic)
    {
        KeyValueTable table = { {"key1", "value1"}, {"key2", "value2"} };
        Json::Value value;
        value["key1"] = "value1";
        value["key2"] = "value2";
        KeyValueTable table2 = Json::KeyValueTableFromJson(value);
        ASSERT_EQ(table, table2);
    }

    TEST(JsonUtilTest, StringFromBoolBasic)
    {
        EXPECT_EQ(Json::StringFromBool(true), "t");
        EXPECT_EQ(Json::StringFromBool(false), "f");
    }

    TEST(JsonUtilTest, JsonFromStringArrayBasic)
    {
        StringArray input{ "one", "two", "three" };
        Json::Value value = Json::JsonFromStringArray(input);

        ASSERT_TRUE(value.IsArray());
        ASSERT_EQ(value.GetArray().size(), 3u);
        EXPECT_THAT(value.GetArray(),
                    ElementsAre(
                        Json::Value("one"),
                        Json::Value("two"),
                        Json::Value("three")));
        Json::Value comp = Json::Value(StringArray{ "one", "two", "three" });
        ASSERT_EQ(value, comp);
    }

    TEST(JsonUtilTest, JsonFromArrayOfJsonBasic)
    {
        // Build array of JSON objects
        Json::Value obj1; obj1["a"] = "1";
        Json::Value obj2; obj2["b"] = "2";
        std::vector<Json::Value> input{ obj1, obj2 };

        Json::Value value = Json::JsonFromArrayOfJson(input);

        // Validate structure
        ASSERT_TRUE(value.IsArray());
        ASSERT_EQ(value.GetArray().size(), 2u);
        EXPECT_EQ(value[0]["a"].Get<std::string>(), "1");
        EXPECT_EQ(value[1]["b"].Get<std::string>(), "2");

        // Compare with expected constructed literal
        Json::Value expected;
        expected = std::vector<Json::Value>{ obj1, obj2 };
        EXPECT_EQ(value, expected);
    }

    TEST(JsonUtilTest, JsonFromDataResultsBasic)
    {
        // Construct a DataResults with unsorted columns to ensure sorting is handled
        StringArray cols{ "b", "c", "a" };
        StringTable rows{
            { "b1", "c1", "a1" },
            { "b2", "c2", "a2" }
        };
        DataResults dr = MakeDataResults(cols, rows);

        Json::Value value = Json::JsonFromDataResults(dr);

        // Validate keys and nested content
        ASSERT_TRUE(value.HasChildren());
        ASSERT_EQ(value["sortedColumnNames"].GetArray().size(), 3u);
        ASSERT_EQ(value["dataTable"].GetArray().size(), 2u);

        // sortedColumnNames should be ["a","b","c"]
        Json::Value rCols = value["sortedColumnNames"];
        ASSERT_TRUE(rCols.IsArray());
        ASSERT_EQ(rCols.GetArray().size(), 3u);
        EXPECT_EQ(rCols[0].Get<std::string>(), "a");
        EXPECT_EQ(rCols[1].Get<std::string>(), "b");
        EXPECT_EQ(rCols[2].Get<std::string>(), "c");

        // dataTable should be a list of lists with rows mapped to sorted columns
        Json::Value rTable = value["dataTable"];
        ASSERT_TRUE(rTable.IsArray());
        ASSERT_EQ(rTable.GetArray().size(), 2u);

        // First row: ["a1","b1","c1"]
        ASSERT_TRUE(rTable[0].IsArray());
        ASSERT_EQ(rTable[0].GetArray().size(), 3u);
        EXPECT_EQ(rTable[0][0].Get<std::string>(), "a1");
        EXPECT_EQ(rTable[0][1].Get<std::string>(), "b1");
        EXPECT_EQ(rTable[0][2].Get<std::string>(), "c1");

        // Second row: ["a2","b2","c2"]
        ASSERT_TRUE(rTable[1].IsArray());
        ASSERT_EQ(rTable[1].GetArray().size(), 3u);
        EXPECT_EQ(rTable[1][0].Get<std::string>(), "a2");
        EXPECT_EQ(rTable[1][1].Get<std::string>(), "b2");
        EXPECT_EQ(rTable[1][2].Get<std::string>(), "c2");

        // Also verify using EqualJsonValues with an expected structure
        Json::Value expected;
        expected["sortedColumnNames"] = Json::JsonFromStringArray({ "a", "b", "c" });
        expected["dataTable"] = Json::JsonFromArrayOfJson({
            Json::JsonFromStringArray({ "a1", "b1", "c1" }),
            Json::JsonFromStringArray({ "a2", "b2", "c2" })
        });
        EXPECT_EQ(value, expected);
    }

    TEST(JsonUtilTest, JsonFromDataResultsWithCountBasic)
    {
        // Construct a DataResultsWithCount
        StringArray cols{ "b", "c", "a" };
        StringTable rows{
            { "b1", "c1", "a1" },
            { "b2", "c2", "a2" }
        };
        DataResultsWithCount drwc;
        drwc.dataResults = MakeDataResults(cols, rows);
        drwc.totalCount = 47;

        Json::Value value = Json::JsonFromDataResultsWithCount(drwc);

        // Validate keys and nested content
        ASSERT_TRUE(value.HasChildren());
        ASSERT_EQ(value["sortedColumnNames"].GetArray().size(), 3u);
        ASSERT_EQ(value["dataTable"].GetArray().size(), 2u);

        // Verify totalCount is included
        ASSERT_TRUE(value["totalCount"].Is<int64_t>());
        EXPECT_EQ(value["totalCount"].Get<int64_t>(), 47);

        // sortedColumnNames should be ["a","b","c"]
        Json::Value rCols = value["sortedColumnNames"];
        ASSERT_TRUE(rCols.IsArray());
        ASSERT_EQ(rCols.GetArray().size(), 3u);
        EXPECT_EQ(rCols[0].Get<std::string>(), "a");
        EXPECT_EQ(rCols[1].Get<std::string>(), "b");
        EXPECT_EQ(rCols[2].Get<std::string>(), "c");

        // dataTable should be a list of lists with rows mapped to sorted columns
        Json::Value rTable = value["dataTable"];
        ASSERT_TRUE(rTable.IsArray());
        ASSERT_EQ(rTable.GetArray().size(), 2u);

        // Verify using EqualJsonValues with an expected structure
        Json::Value expected;
        expected["sortedColumnNames"] = Json::JsonFromStringArray({ "a", "b", "c" });
        expected["dataTable"] = Json::JsonFromArrayOfJson({
            Json::JsonFromStringArray({ "a1", "b1", "c1" }),
            Json::JsonFromStringArray({ "a2", "b2", "c2" })
        });
        expected["totalCount"] = int64_t(47);
        EXPECT_EQ(value, expected);
    }

} // namespace {
