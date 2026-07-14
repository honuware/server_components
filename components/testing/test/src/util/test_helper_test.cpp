#include "test_helper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

TEST(TestHelperTest, CompareDataResultsMinusColumnsBasic) {
    // Same columns and same data -> true
    StringArray cols = { "id", "email", "first_name" };
    StringTable rows = {
        {"1", "a@example.com", "Alice"},
        {"2", "b@example.com", "Bob"}
    };
    DataResults dr1 = MakeDataResults(cols, rows);
    DataResults dr2 = MakeDataResults(cols, rows);
    EXPECT_TRUE(CompareDataResultsMinusColumns(dr1, dr2));
}

TEST(TestHelperTest, CompareDataResultsMinusColumnsColumnsRemoved) {
    // Values differ in removed column but equal in the rest -> true
    StringArray cols = { "id", "email", "updated_at" };
    StringTable rows1 = {
        {"1", "x@example.com", "2024-01-01T00:00:00Z"},
        {"2", "y@example.com", "2024-01-01T00:05:00Z"}
    };
    StringTable rows2 = {
        {"1", "x@example.com", "2025-01-01T00:00:00Z"}, // different updated_at
        {"2", "y@example.com", "2025-01-01T00:05:00Z"}
    };
    DataResults dr1 = MakeDataResults(cols, rows1);
    DataResults dr2 = MakeDataResults(cols, rows2);

    EXPECT_TRUE(CompareDataResultsMinusColumns(dr1, dr2, "updated_at"));
}

TEST(TestHelperTest, CompareDataResultsMinusColumnsColumnMismatch) {
    // Second has an extra non-removed column -> false
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
    DataResults dr1 = MakeDataResults(cols1, rows1);
    DataResults dr2 = MakeDataResults(cols2, rows2);

    EXPECT_FALSE(CompareDataResultsMinusColumns(dr1, dr2));
}

TEST(TestHelperTest, CompareDataResultsMinusColumnsDataMismatch) {
    // Same kept columns but different value in a kept column -> false
    StringArray cols = { "id", "email", "updated_at" };
    StringTable rows1 = {
        {"1", "x@example.com", "t1"},
        {"2", "y@example.com", "t2"}
    };
    StringTable rows2 = {
        {"1", "x_changed@example.com", "t1"},
        {"2", "y@example.com", "t2"}
    };
    DataResults dr1 = MakeDataResults(cols, rows1);
    DataResults dr2 = MakeDataResults(cols, rows2);

    EXPECT_FALSE(CompareDataResultsMinusColumns(dr1, dr2, "updated_at"));
}

TEST(TestHelperTest, CompareKeyValueTablesMinusColumns) {
    // Same columns and same data -> true
    KeyValueTable kv1 = {
        {"id", "1"},
        {"blah1", "value1"},
        {"email", "a@example.com"}
    };
    KeyValueTable kv2 = {
        {"id", "1"},
        {"blah2", "value2"},
        {"email", "a@example.com"}
    };
    EXPECT_TRUE(CompareKeyValueTablesMinusColumns(kv1, kv2, "blah1", "blah2"));
    EXPECT_FALSE(CompareKeyValueTablesMinusColumns(kv1, kv2));
}

TEST(TestHelperTest, CompareKeyValueTableArraysMinusColumns) {
    // Same columns and same data -> true
    KeyValueTableArray kva1 = {
        {
            {"id1", "1"},
            {"blah1", "value1"},
            {"email1", "a@example.com"}
        },
        {
            {"id2", "2"},
            {"blah2", "value2"},
            {"email2", "b@example.com"}
        }
    };
    KeyValueTableArray kva2 = {
        {
            {"id1", "1"},
            {"blah3", "value3"},
            {"email1", "a@example.com"}
        },
        {
            {"id2", "2"},
            {"blah4", "value4"},
            {"email2", "b@example.com"}
        }
    };
    EXPECT_TRUE(CompareKeyValueTableArraysMinusColumns(kva1, kva2, "blah1", "blah2", "blah3", "blah4"));
    EXPECT_FALSE(CompareKeyValueTableArraysMinusColumns(kva1, kva2));
}

}  // namespace
