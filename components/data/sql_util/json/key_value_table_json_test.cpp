#include "sql_util/json/key_value_table_json.h"

#include <gtest/gtest.h>

namespace SqlUtil {
namespace {

TEST(KeyValueTableJsonTest, KeyValueTableToJsonStrings) {
    KeyValueTable table;
    table["name"] = "Test Name";
    table["email"] = "test@example.com";
    table["status"] = "active";

    auto json = KeyValueTableToJson(table);

    EXPECT_EQ(json["name"].Get<std::string>(), "Test Name");
    EXPECT_EQ(json["email"].Get<std::string>(), "test@example.com");
    EXPECT_EQ(json["status"].Get<std::string>(), "active");
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonIntegers) {
    KeyValueTable table;
    table["id"] = "123";
    table["amount_cents"] = "5000";
    table["count"] = "42";

    auto json = KeyValueTableToJson(table);

    EXPECT_EQ(json["id"].Get<int64_t>(), 123);
    EXPECT_EQ(json["amount_cents"].Get<int64_t>(), 5000);
    EXPECT_EQ(json["count"].Get<int64_t>(), 42);
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonNegativeIntegers) {
    KeyValueTable table;
    table["refund_amount"] = "-2500";
    table["adjustment"] = "-100";

    auto json = KeyValueTableToJson(table);

    EXPECT_EQ(json["refund_amount"].Get<int64_t>(), -2500);
    EXPECT_EQ(json["adjustment"].Get<int64_t>(), -100);
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonMixed) {
    KeyValueTable table;
    table["id"] = "123";
    table["name"] = "Product Name";
    table["price_cents"] = "9999";
    table["currency"] = "USD";
    table["status"] = "active";

    auto json = KeyValueTableToJson(table);

    // Integers
    EXPECT_EQ(json["id"].Get<int64_t>(), 123);
    EXPECT_EQ(json["price_cents"].Get<int64_t>(), 9999);

    // Strings
    EXPECT_EQ(json["name"].Get<std::string>(), "Product Name");
    EXPECT_EQ(json["currency"].Get<std::string>(), "USD");
    EXPECT_EQ(json["status"].Get<std::string>(), "active");
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonEmptyTable) {
    KeyValueTable table;

    auto json = KeyValueTableToJson(table);

    // An empty table produces an empty JSON object
    EXPECT_TRUE(json.HasChildren());  // It's still a JSON object type
    EXPECT_EQ(json.GetChildren().size(), 0u);  // But with no keys
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonEmptyStringValue) {
    KeyValueTable table;
    table["empty"] = "";
    table["id"] = "1";

    auto json = KeyValueTableToJson(table);

    // Empty string should remain as string
    EXPECT_EQ(json["empty"].Get<std::string>(), "");
    EXPECT_EQ(json["id"].Get<int64_t>(), 1);
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonLargeIntegers) {
    KeyValueTable table;
    table["large_id"] = "9007199254740991";  // Max safe integer in JavaScript
    table["timestamp"] = "1704067200000000";  // Microseconds timestamp

    auto json = KeyValueTableToJson(table);

    EXPECT_EQ(json["large_id"].Get<int64_t>(), 9007199254740991LL);
    EXPECT_EQ(json["timestamp"].Get<int64_t>(), 1704067200000000LL);
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonZero) {
    KeyValueTable table;
    table["zero"] = "0";
    table["negative_zero"] = "-0";

    auto json = KeyValueTableToJson(table);

    EXPECT_EQ(json["zero"].Get<int64_t>(), 0);
    // "-0" is not a valid integer string (has minus with only zero),
    // but our implementation may still handle it
}

TEST(KeyValueTableJsonTest, KeyValueTableToJsonNonIntegerStrings) {
    KeyValueTable table;
    table["float_like"] = "123.45";
    table["mixed"] = "abc123";
    table["uuid"] = "550e8400-e29b-41d4-a716-446655440000";
    table["phone"] = "+1-555-1234";

    auto json = KeyValueTableToJson(table);

    // All should remain as strings since they're not pure integers
    EXPECT_EQ(json["float_like"].Get<std::string>(), "123.45");
    EXPECT_EQ(json["mixed"].Get<std::string>(), "abc123");
    EXPECT_EQ(json["uuid"].Get<std::string>(), "550e8400-e29b-41d4-a716-446655440000");
    EXPECT_EQ(json["phone"].Get<std::string>(), "+1-555-1234");
}

TEST(KeyValueTableJsonTest, KeyValueTableArrayToJsonEmpty) {
    KeyValueTableArray tableArray;

    auto json = KeyValueTableArrayToJson(tableArray);

    EXPECT_TRUE(json.IsArray());
    EXPECT_EQ(json.GetArray().size(), 0u);
}

TEST(KeyValueTableJsonTest, KeyValueTableArrayToJsonSingleItem) {
    KeyValueTableArray tableArray;

    KeyValueTable table;
    table["id"] = "1";
    table["name"] = "First";
    tableArray.push_back(table);

    auto json = KeyValueTableArrayToJson(tableArray);

    EXPECT_TRUE(json.IsArray());
    EXPECT_EQ(json.GetArray().size(), 1u);
    EXPECT_EQ(json.GetArray()[0]["id"].Get<int64_t>(), 1);
    EXPECT_EQ(json.GetArray()[0]["name"].Get<std::string>(), "First");
}

TEST(KeyValueTableJsonTest, KeyValueTableArrayToJsonMultipleItems) {
    KeyValueTableArray tableArray;

    KeyValueTable table1;
    table1["id"] = "1";
    table1["status"] = "active";
    tableArray.push_back(table1);

    KeyValueTable table2;
    table2["id"] = "2";
    table2["status"] = "pending";
    tableArray.push_back(table2);

    KeyValueTable table3;
    table3["id"] = "3";
    table3["status"] = "completed";
    tableArray.push_back(table3);

    auto json = KeyValueTableArrayToJson(tableArray);

    EXPECT_TRUE(json.IsArray());
    EXPECT_EQ(json.GetArray().size(), 3u);

    const auto& arr = json.GetArray();
    EXPECT_EQ(arr[0]["id"].Get<int64_t>(), 1);
    EXPECT_EQ(arr[0]["status"].Get<std::string>(), "active");
    EXPECT_EQ(arr[1]["id"].Get<int64_t>(), 2);
    EXPECT_EQ(arr[1]["status"].Get<std::string>(), "pending");
    EXPECT_EQ(arr[2]["id"].Get<int64_t>(), 3);
    EXPECT_EQ(arr[2]["status"].Get<std::string>(), "completed");
}

TEST(KeyValueTableJsonTest, KeyValueTableArrayToJsonDifferentSchemas) {
    KeyValueTableArray tableArray;

    KeyValueTable table1;
    table1["id"] = "1";
    table1["name"] = "Product A";
    tableArray.push_back(table1);

    KeyValueTable table2;
    table2["id"] = "2";
    table2["name"] = "Product B";
    table2["extra_field"] = "extra value";
    tableArray.push_back(table2);

    auto json = KeyValueTableArrayToJson(tableArray);

    EXPECT_EQ(json.GetArray().size(), 2u);

    const auto& arr = json.GetArray();
    // First item doesn't have extra_field
    EXPECT_FALSE(arr[0].HasChild("extra_field", nullptr));
    // Second item has extra_field
    const Json::Value* extraField = nullptr;
    EXPECT_TRUE(arr[1].HasChild("extra_field", &extraField));
    EXPECT_EQ(extraField->Get<std::string>(), "extra value");
}

}  // namespace
}  // namespace SqlUtil
