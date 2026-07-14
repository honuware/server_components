#include "json_test_util.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace JsonTestUtil {
namespace {

static Json::Value MakeJsonFromColumnsAndRows(
    const StringArray& cols, const StringTable& rows) {
	Json::JsonArray jcols;
	jcols.reserve(cols.size());
	for (const auto& c : cols) jcols.emplace_back(std::string(c));

	Json::JsonArray jrows;
	jrows.reserve(rows.size());
	for (const auto& row : rows) {
		Json::JsonArray jrow;
		jrow.reserve(row.size());
		for (const auto& cell : row) jrow.emplace_back(std::string(cell));
		jrows.emplace_back(std::move(jrow));
	}

	Json::JsonObject out;
	out["sortedColumnNames"] = std::move(jcols);
	out["dataTable"] = std::move(jrows);
	return Json::Value(std::move(out));
}

TEST(JsonTestUtilTest, CompareJsonDataResultsMinusColumnsBasic) {
    StringArray cols = { "id", "email", "first_name" };
    StringTable rows = {
        {"1", "a@example.com", "Alice"},
        {"2", "b@example.com", "Bob"}
    };
    auto dr1 = MakeJsonFromColumnsAndRows(cols, rows);
    auto dr2 = MakeJsonFromColumnsAndRows(cols, rows);
    EXPECT_TRUE(CompareJsonDataResultsMinusColumns(dr1, dr2));
}

TEST(JsonTestUtilTest, CompareJsonDataResultsMinusColumnsColumnsRemoved) {
    // Different updated_at but ignored -> true
    StringArray cols = { "id", "email", "updated_at" };
    StringTable rows1 = {
        {"1", "x@example.com", "2024-01-01T00:00:00Z"},
        {"2", "y@example.com", "2024-01-01T00:05:00Z"}
    };
    StringTable rows2 = {
        {"1", "x@example.com", "2025-01-01T00:00:00Z"},
        {"2", "y@example.com", "2025-01-01T00:05:00Z"}
    };
    auto dr1 = MakeJsonFromColumnsAndRows(cols, rows1);
    auto dr2 = MakeJsonFromColumnsAndRows(cols, rows2);
    EXPECT_TRUE(CompareJsonDataResultsMinusColumns(dr1, dr2, "updated_at"));
}

TEST(JsonTestUtilTest, CompareJsonDataResultsMinusColumnsColumnMismatch) {
    // dr2 has extra non-removed column -> false
    StringArray cols1 = { "id", "email" };
    StringArray cols2 = { "id", "email", "first_name" };
    StringTable rows1 = {
        {"1", "a@example.com"},
        {"2", "b@example.com"}
    };
    StringTable rows2 = {
        {"1", "a@example.com", "Alice"},
        {"2", "b@example.com", "Bob"}
    };
    auto dr1 = MakeJsonFromColumnsAndRows(cols1, rows1);
    auto dr2 = MakeJsonFromColumnsAndRows(cols2, rows2);
    EXPECT_FALSE(CompareJsonDataResultsMinusColumns(dr1, dr2));
}

TEST(JsonTestUtilTest, CompareJsonDataResultsMinusColumnsDataMismatch) {
    // Same kept columns but value differs in kept column -> false
    StringArray cols = { "id", "email", "updated_at" };
    StringTable rows1 = {
        {"1", "x@example.com", "t1"},
        {"2", "y@example.com", "t2"}
    };
    StringTable rows2 = {
        {"1", "x_changed@example.com", "t1"},
        {"2", "y@example.com", "t2"}
    };
    auto dr1 = MakeJsonFromColumnsAndRows(cols, rows1);
    auto dr2 = MakeJsonFromColumnsAndRows(cols, rows2);
    EXPECT_FALSE(CompareJsonDataResultsMinusColumns(dr1, dr2, "updated_at"));
}

TEST(JsonTestUtilTest, CompareJsonObjectMinusColumnsHelperBasic) {
	Json::JsonObject obj1;
	obj1["a"] = "1";
	obj1["b"] = "2";
	obj1["c"] = "3";

	Json::JsonObject obj2;
	obj2["a"] = "1";
	obj2["b"] = "2";
	obj2["c"] = "3";

	EXPECT_TRUE(CompareJsonObjectMinusColumns(Json::Value(std::move(obj1)), Json::Value(std::move(obj2))));
}

TEST(JsonTestUtilTest, CompareJsonObjectMinusColumnsHelperRemoved) {
    // Objects differ on removed field; should still be equal
	Json::JsonObject obj1;
	obj1["id"] = "1";
	obj1["email"] = "x@example.com";
	obj1["updated_at"] = "t1";

	Json::JsonObject obj2;
	obj2["id"] = "1";
	obj2["email"] = "x@example.com";
	obj2["updated_at"] = "t2";

	EXPECT_TRUE(CompareJsonObjectMinusColumns(Json::Value(std::move(obj1)), Json::Value(std::move(obj2)), "updated_at"));
}

TEST(JsonTestUtilTest, CompareJsonObjectMinusColumnsHelperColumnsMismatch) {
    // obj2 has an extra non-removed field
	Json::JsonObject obj1;
	obj1["a"] = "1";
	obj1["b"] = "2";

	Json::JsonObject obj2;
	obj2["a"] = "1";
	obj2["b"] = "2";
	obj2["c"] = "3";

	EXPECT_FALSE(CompareJsonObjectMinusColumns(Json::Value(std::move(obj1)), Json::Value(std::move(obj2))));
}

TEST(JsonTestUtilTest, CompareJsonObjectMinusColumnsHelperDataMismatch) {
    // Same kept keys but different value in kept key -> false
	Json::JsonObject obj1;
	obj1["id"] = "1";
	obj1["email"] = "x@example.com";
	obj1["updated_at"] = "t1";

	Json::JsonObject obj2;
	obj2["id"] = "2";
	obj2["email"] = "x@example.com";
	obj2["updated_at"] = "t2";

	EXPECT_FALSE(CompareJsonObjectMinusColumns(Json::Value(std::move(obj1)), Json::Value(std::move(obj2)), "updated_at"));
}

}  // namespace
}  // namespace JsonTestUtil