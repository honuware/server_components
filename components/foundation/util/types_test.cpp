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
    using ::testing::Not;
    using ::testing::ContainerEq;

    TEST(TypesTest, MakeKeyValueTableBasic)
    {
        constexpr std::string_view kKey1 = "key1";
        constexpr std::string_view kValue1 = "value1";
        constexpr std::string_view kKey2 = "key2";
        constexpr std::string_view kValue2 = "value2";
        KeyValueTable keyValueTable = { 
            {(std::string)kKey1, (std::string)kValue1},
            {(std::string)kKey2, (std::string)kValue2} 
        };
        EXPECT_THAT(
            MakeKeyValueTable({ {kKey1, kValue1}, {kKey2, kValue2} }),
            ContainerEq(keyValueTable));
    }

    TEST(TypesTest, AddToKeyValueTableBasic)
    {
        constexpr std::string_view kKey1 = "key1";
        constexpr std::string_view kValue1 = "value1";
        constexpr std::string_view kKey2 = "key2";
        constexpr std::string_view kValue2 = "value2";
        KeyValueTable keyValueTable;
        KeyValueTable keyValueTableComp = {
            {(std::string)kKey1, (std::string)kValue1},
            {(std::string)kKey2, (std::string)kValue2}
        };
        AddToKeyValueTable(keyValueTable, kKey1, kValue1);
        AddToKeyValueTable(keyValueTable, kKey2, kValue2);
        EXPECT_THAT(
            keyValueTable,
            ContainerEq(keyValueTableComp));
    }

    TEST(TypesTest, MakeDataResultsBasic)
    {
        StringArray stringArray = { "b", "c", "a" };
        StringTable stringTable = { 
            { "b1", "c1", "a1" }, { "b2", "c2", "a2" } };
        DataResults dataResults = MakeDataResults(stringArray, stringTable);
        EXPECT_THAT(dataResults.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dataResults.dataTable.size(), 2);
        EXPECT_THAT(dataResults.dataTable[0], ElementsAre("a1", "b1", "c1"));
        EXPECT_THAT(dataResults.dataTable[1], ElementsAre("a2", "b2", "c2"));
    }

    TEST(TypesTest, MakeDataResultsFromKeyValueTableArrayBasic)
    {
        KeyValueTableArray keyValueTableArray = {
            { {"b", "b1"}, {"c", "c1"}, {"a", "a1"} },
            { {"b", "b2"}, {"c", "c2"}, {"a", "a2"} }
        };
        DataResults dataResults = MakeDataResultsFromKeyValueTableArray(keyValueTableArray);
        EXPECT_THAT(dataResults.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dataResults.dataTable.size(), 2);
        EXPECT_THAT(dataResults.dataTable[0], ElementsAre("a1", "b1", "c1"));
        EXPECT_THAT(dataResults.dataTable[1], ElementsAre("a2", "b2", "c2"));
    }

    TEST(TypesTest, MakeDataResultsStringViewArrayBasic)
    {
        constexpr std::string_view svb = "b";
        constexpr std::string_view svc = "c";
        constexpr std::string_view sva = "a";
        StringViewArray stringArray = { svb, svc, sva };
        StringTable stringTable = {
            { "b1", "c1", "a1" }, { "b2", "c2", "a2" } };
        DataResults dataResults = MakeDataResultsStringView(stringArray, stringTable);
        EXPECT_THAT(dataResults.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dataResults.dataTable.size(), 2);
        EXPECT_THAT(dataResults.dataTable[0], ElementsAre("a1", "b1", "c1"));
        EXPECT_THAT(dataResults.dataTable[1], ElementsAre("a2", "b2", "c2"));
    }

    TEST(TypesTest, MakeDataResultsKeyValueTableBasic)
    {
        KeyValueTable keyValueTable = {
            { "b", "b1" }, { "c", "c1" }, { "a", "a1" } };
        DataResults dataResults = MakeDataResults(keyValueTable);
        EXPECT_THAT(dataResults.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dataResults.dataTable.size(), 1);
        EXPECT_THAT(dataResults.dataTable[0], ElementsAre("a1", "b1", "c1"));
    }

    TEST(TypesTest, GetDataResultsValueBasic)
    {
        // Build DataResults with unsorted columns to ensure name-based lookup respects sorting
        StringArray cols = { "b", "c", "a" };
        StringTable rows = {
            {"b1", "c1", "a1"},
            {"b2", "c2", "a2"}
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dr.dataTable.size(), 2u);

        // Row 0
        EXPECT_THAT(GetDataResultsValue(dr, "a", 0), Eq("a1"));
        EXPECT_THAT(GetDataResultsValue(dr, "b", 0), Eq("b1"));
        EXPECT_THAT(GetDataResultsValue(dr, "c", 0), Eq("c1"));

        // Row 1
        EXPECT_THAT(GetDataResultsValue(dr, "a", 1), Eq("a2"));
        EXPECT_THAT(GetDataResultsValue(dr, "b", 1), Eq("b2"));
        EXPECT_THAT(GetDataResultsValue(dr, "c", 1), Eq("c2"));

        // Missing column => empty string
        EXPECT_THAT(GetDataResultsValue(dr, "missing", 0), Eq(""));
        // Out-of-range row index => empty string
        EXPECT_THAT(GetDataResultsValue(dr, "a", 2), Eq(""));
    }

    TEST(TypesTest, GetDataResultsValueAsIntBasic)
    {
        StringArray cols = { "x", "y" };
        StringTable rows = {
            {"10", "20"},
            {"-5", "0"}
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("x", "y"));
        ASSERT_EQ(dr.dataTable.size(), 2u);

        EXPECT_EQ(GetDataResultsValueAsInt(dr, "x", 0), 10);
        EXPECT_EQ(GetDataResultsValueAsInt(dr, "y", 0), 20);
        EXPECT_EQ(GetDataResultsValueAsInt(dr, "x", 1), -5);
        EXPECT_EQ(GetDataResultsValueAsInt(dr, "y", 1), 0);

        // Invalid numeric should throw
        StringArray colsBad = { "n" };
        StringTable rowsBad = { {"abc"} };
        DataResults drBad = MakeDataResults(colsBad, rowsBad);
        EXPECT_THROW((void)GetDataResultsValueAsInt(drBad, "n", 0), std::invalid_argument);
    }

    TEST(TypesTest, GetDataResultsValueAsInt64Basic)
    {
        // Use values within int64 range
        StringArray cols = { "z" };
        StringTable rows = {
            { "9223372036854775806" }, // max-1
            { "-9223372036854775807" } // min+1
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("z"));
        ASSERT_EQ(dr.dataTable.size(), 2u);

        EXPECT_EQ(GetDataResultsValueAsInt64(dr, "z", 0), 9223372036854775806LL);
        EXPECT_EQ(GetDataResultsValueAsInt64(dr, "z", 1), -9223372036854775807LL);

        // Invalid numeric should throw
        StringArray colsBad = { "n64" };
        StringTable rowsBad = { {"xyz"} };
        DataResults drBad = MakeDataResults(colsBad, rowsBad);
        EXPECT_THROW((void)GetDataResultsValueAsInt64(drBad, "n64", 0), std::invalid_argument);
    }

    TEST(TypesTest, GetDataResultsValueAsBoolBasic)
    {
        // StringToBool: true for "t","true","1" (case-insensitive); false otherwise
        StringArray cols = { "v" };
        StringTable rows = {
            {"true"},
            {"TRUE"},
            {"t"},
            {"1"},
            {"false"},
            {"0"},
            {"no"},
            {""}
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("v"));
        ASSERT_EQ(dr.dataTable.size(), 8u);

        EXPECT_TRUE(GetDataResultsValueAsBool(dr, "v", 0));
        EXPECT_TRUE(GetDataResultsValueAsBool(dr, "v", 1));
        EXPECT_TRUE(GetDataResultsValueAsBool(dr, "v", 2));
        EXPECT_TRUE(GetDataResultsValueAsBool(dr, "v", 3));
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "v", 4));
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "v", 5));
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "v", 6));
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "v", 7));

        // Missing column -> empty string -> false
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "missing", 0));
        // Out-of-range index -> empty string -> false
        EXPECT_FALSE(GetDataResultsValueAsBool(dr, "v", 100));
    }

    TEST(TypesTest, GetDataResultsValueIsEmptyBasic)
    {
        // StringToBool: true for "t","true","1" (case-insensitive); false otherwise
        StringArray cols = { "v" };
        StringTable rows = {
            {std::string()},
            {"good"},
            {""}
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("v"));
        ASSERT_EQ(dr.dataTable.size(), 3);

        EXPECT_TRUE(GetDataResultsValueIsEmpty(dr, "v", 0));
        EXPECT_FALSE(GetDataResultsValueIsEmpty(dr, "v", 1));
        EXPECT_TRUE(GetDataResultsValueIsEmpty(dr, "v", 2));

        // Missing column -> empty string
        EXPECT_TRUE(GetDataResultsValueIsEmpty(dr, "missing", 0));
        // Out-of-range index -> empty string -> true
        EXPECT_TRUE(GetDataResultsValueIsEmpty(dr, "v", 100));
    }

    TEST(TypesTest, KeyValueTableArrayFromDataResultsBasic)
    {
        // Build DataResults with unsorted columns to verify sorting is respected
        StringArray cols = { "b", "c", "a" };
        StringTable rows = {
            {"b1", "c1", "a1"},
            {"b2", "c2", "a2"}
        };
        DataResults dr = MakeDataResults(cols, rows);
        // Sanity check of MakeDataResults behavior
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dr.dataTable.size(), 2u);
        ASSERT_THAT(dr.dataTable[0], ElementsAre("a1", "b1", "c1"));
        ASSERT_THAT(dr.dataTable[1], ElementsAre("a2", "b2", "c2"));

        KeyValueTableArray kva = KeyValueTableArrayFromDataResults(dr);

        ASSERT_EQ(kva.size(), 2u);
        KeyValueTable expected1 = { {"a","a1"}, {"b","b1"}, {"c","c1"} };
        KeyValueTable expected2 = { {"a","a2"}, {"b","b2"}, {"c","c2"} };

        EXPECT_THAT(kva[0], ContainerEq(expected1));
        EXPECT_THAT(kva[1], ContainerEq(expected2));
    }

    TEST(TypesTest, StringArrayFromDataResultsColumnBasic)
    {
        // Build DataResults with unsorted columns to ensure sorting is handled
        StringArray cols = { "b", "c", "a" };
        StringTable rows = {
            {"b1", "c1", "a1"},
            {"b2", "c2", "a2"}
        };
        DataResults dr = MakeDataResults(cols, rows);
        ASSERT_THAT(dr.sortedColumnNames, ElementsAre("a", "b", "c"));
        ASSERT_EQ(dr.dataTable.size(), 2u);

        // Default column index (0) should return the "a" column values
        StringArray colA = StringArrayFromDataResultsColumn(dr);
        EXPECT_THAT(colA, ElementsAre("a1", "a2"));

        // Column 1 => "b" values
        StringArray colB = StringArrayFromDataResultsColumn(dr, 1);
        EXPECT_THAT(colB, ElementsAre("b1", "b2"));

        // Column 2 => "c" values
        StringArray colC = StringArrayFromDataResultsColumn(dr, 2);
        EXPECT_THAT(colC, ElementsAre("c1", "c2"));

        // Out-of-range column returns empty
        StringArray colOut = StringArrayFromDataResultsColumn(dr, 3);
        EXPECT_TRUE(colOut.empty());
    }

    TEST(TypesTest, StringArrayFromKeyValueTableArrayColumnBasic)
    {
        KeyValueTableArray kva = {
            { {"b", "b1"}, {"c", "c1"}, {"a", "a1"} },
            { {"b", "b2"}, {"c", "c2"}, {"a", "a2"} }
        };
        StringArray colA = StringArrayFromKeyValueTableArrayColumn(kva, 0);
        EXPECT_THAT(colA, ElementsAre("a1", "a2"));
        StringArray colB = StringArrayFromKeyValueTableArrayColumn(kva, 1);
        EXPECT_THAT(colB, ElementsAre("b1", "b2"));
        StringArray colC = StringArrayFromKeyValueTableArrayColumn(kva, 2);
        EXPECT_THAT(colC, ElementsAre("c1", "c2"));
        EXPECT_THROW(StringArrayFromKeyValueTableArrayColumn(kva, 3), std::out_of_range);
    }

    TEST(TypesTest, StringSetFromStringArrayBasic)
    {
        StringArray values = { "value1", "value2" };
        StringSet stringSet = StringSetFromStringArray(values);
        EXPECT_THAT(stringSet, Contains("value1"));
        EXPECT_THAT(stringSet, Contains("value2"));
        EXPECT_THAT(stringSet, Not(Contains("value3")));
    }

    TEST(TypesTest, AddStringArrayToSetBasic)
    {
        StringArray values1 = { "value1", "value2" };
        StringArray values2 = { "value2", "value3" };
        StringSet stringSet;
        EXPECT_THAT(stringSet, Not(Contains("value1")));
        AddStringArrayToSet(stringSet, values1);
        EXPECT_THAT(stringSet, Contains("value1"));
        EXPECT_THAT(stringSet, Contains("value2"));
        EXPECT_THAT(stringSet, Not(Contains("value3")));
        AddStringArrayToSet(stringSet, values2);
        EXPECT_THAT(stringSet, Contains("value1"));
        EXPECT_THAT(stringSet, Contains("value2"));
        EXPECT_THAT(stringSet, Contains("value3"));
    }

    TEST(TypesTest, SetContainsBasic)
    {
        StringSet stringSet;
        ASSERT_FALSE(SetContains(stringSet, "value1"));
        stringSet.insert("value1");
        ASSERT_TRUE(SetContains(stringSet, "value1"));
    }

    TEST(TypesTest, StringArrayToCommaSeparatedStringBasic)
    {
        EXPECT_THAT(
            StringArrayToCommaSeparatedString({ "table1", "table2", "table3" }),
            Eq("table1, table2, table3"));
        EXPECT_THAT(
            StringArrayToCommaSeparatedString({ "table1", "table2" }),
            Eq("table1, table2"));
        EXPECT_THAT(
            StringArrayToCommaSeparatedString({ "table1" }),
            Eq("table1"));
        EXPECT_THAT(
            StringArrayToCommaSeparatedString({}),
            Eq(""));
    }

    TEST(TypesTest, StringToLowerBasic) {
        EXPECT_THAT(StringToLower("ABCdef123!@"), Eq("abcdef123!@"));
        EXPECT_THAT(StringToLower("abcd"), Eq("abcd"));
        EXPECT_THAT(StringToLower("ABCD"), Eq("abcd"));
        EXPECT_THAT(StringToLower("AbAb"), Eq("abab"));
    }

    TEST(TypesTest, StringTrimBasic) {
        EXPECT_THAT(StringTrim("hello"), Eq("hello"));
        EXPECT_THAT(StringTrim(" hello"), Eq("hello"));
        EXPECT_THAT(StringTrim("hello "), Eq("hello"));
        EXPECT_THAT(StringTrim("  hello  "), Eq("hello"));
    }

    TEST(TypesTest, StringTrimEmptyAndAllWhitespace) {
        EXPECT_THAT(StringTrim(""), Eq(""));
        EXPECT_THAT(StringTrim("   "), Eq(""));
        EXPECT_THAT(StringTrim("\t\t"), Eq(""));
        EXPECT_THAT(StringTrim("\n\r\v\f \t"), Eq(""));
    }

    TEST(TypesTest, StringTrimAllWhitespaceTypes) {
        // ASCII whitespace per std::isspace: space, \t, \n, \r, \v, \f.
        EXPECT_THAT(StringTrim("\thello"), Eq("hello"));
        EXPECT_THAT(StringTrim("hello\t"), Eq("hello"));
        EXPECT_THAT(StringTrim("\nhello\n"), Eq("hello"));
        EXPECT_THAT(StringTrim("\rhello\r"), Eq("hello"));
        EXPECT_THAT(StringTrim("\vhello\v"), Eq("hello"));
        EXPECT_THAT(StringTrim("\fhello\f"), Eq("hello"));
        EXPECT_THAT(StringTrim(" \t\n\rhello \t\n\r"), Eq("hello"));
    }

    TEST(TypesTest, StringTrimDoesNotTouchInteriorWhitespace) {
        EXPECT_THAT(StringTrim("a b c"), Eq("a b c"));
        EXPECT_THAT(StringTrim("  a b c  "), Eq("a b c"));
        EXPECT_THAT(StringTrim("\thello\tworld\t"), Eq("hello\tworld"));
    }

    TEST(TypesTest, StringTrimSingleNonWhitespaceCharacter) {
        EXPECT_THAT(StringTrim("a"), Eq("a"));
        EXPECT_THAT(StringTrim(" a "), Eq("a"));
    }

    TEST(TypesTest, StringTrimSingleWhitespaceCharacter) {
        EXPECT_THAT(StringTrim(" "), Eq(""));
        EXPECT_THAT(StringTrim("\t"), Eq(""));
    }

    TEST(TypesTest, StringToBoolBasic) {
        EXPECT_TRUE(StringToBool("true"));
        EXPECT_TRUE(StringToBool("TRUE"));
        EXPECT_TRUE(StringToBool("TrUe"));
        EXPECT_TRUE(StringToBool("T"));
        EXPECT_TRUE(StringToBool("t"));
        EXPECT_TRUE(StringToBool("1"));
        EXPECT_FALSE(StringToBool("false"));
        EXPECT_FALSE(StringToBool("FALSE"));
        EXPECT_FALSE(StringToBool("FaLsE"));
        EXPECT_FALSE(StringToBool("yes"));
        EXPECT_FALSE(StringToBool("no"));
        EXPECT_FALSE(StringToBool("0"));
        EXPECT_FALSE(StringToBool(""));
    }

    TEST(TypesTest, FormatStringBasic) {
        KeyValueTable keyValueTable = {
            {"key1", "value1"},
            {"key2", "value2"}
        };
        ASSERT_EQ(
            FormatString("This is a test with {key1} and {key2}.", keyValueTable),
            "This is a test with value1 and value2.");
    }

    TEST(TypesTest, URLEncodeDecodeBasic) {
        constexpr std::string_view original = "This is a test! @# $%^&*()_+";
        std::string encoded = URLEncode(original);
        std::string decoded = URLDecode(encoded);
        EXPECT_THAT(decoded, Eq(original));
    }

    TEST(TypesTest, SplitStringBasic) {
        auto [left, right] = SplitString("key=value", "=");
        EXPECT_THAT(left, Eq("key"));
        EXPECT_THAT(right, Eq("value"));
        auto [left2, right2] = SplitString("novalue", "=");
        EXPECT_THAT(left2, Eq("novalue"));
        EXPECT_THAT(right2, Eq(""));
    }

    TEST(TypesTest, NormalizeCrLfBasic) {
        EXPECT_THAT(
            NormalizeCrLf("Line1\r\nLine2\r\nLine3"),
            Eq("Line1\r\nLine2\r\nLine3"));
        EXPECT_THAT(
            NormalizeCrLf("Line1\nLine2\nLine3"),
            Eq("Line1\r\nLine2\r\nLine3"));
        EXPECT_THAT(
            NormalizeCrLf("Mixed\r\nLines\nAnd\nNewlines"),
            Eq("Mixed\r\nLines\r\nAnd\r\nNewlines"));
    }

    TEST(TypesTest, ContainerContainsBasic) {
        StringSet stringSet;
        stringSet.insert("value1");
        EXPECT_TRUE(ContainerContains(stringSet, "value1"));
        EXPECT_FALSE(ContainerContains(stringSet, "value2"));
        KeyValueTable keyValueTable;
        keyValueTable["key1"] = "value1";
        EXPECT_TRUE(ContainerContains(keyValueTable, "key1"));
        EXPECT_FALSE(ContainerContains(keyValueTable, "key2"));
        StringArray stringArray = { "value1", "value2", "value3" };
        EXPECT_TRUE(ContainerContains(stringArray, "value2"));
        EXPECT_FALSE(ContainerContains(stringArray, "value4"));
    }

    TEST(TypesTest, HashAddBasic)
    {
        int64_t hash1 = HashInit();
        int64_t hash2 = HashInit();
        EXPECT_EQ(hash1, hash2);
        hash1 = HashAdd(hash1, "test string");
        EXPECT_NE(hash1, hash2);
        hash2 = HashAdd(hash2, "test string");
        EXPECT_EQ(hash1, hash2);
        hash1 = HashAdd(hash1, 1234567890);
        hash2 = HashAdd(hash2, 1234567890);
        EXPECT_EQ(hash1, hash2);
    }

} // namespace {
