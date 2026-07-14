#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <initializer_list>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>
#include <cstdint>

using StringArray = std::vector<std::string>;
using StringViewArray = std::vector<std::string_view>;
using StringTable = std::vector<StringArray>;
using StringSet = std::unordered_set<std::string>;
using Blob = std::vector<std::byte>;

using KeyValueTable = std::map<std::string, std::string>;
using KeyValueTableArray = std::vector<KeyValueTable>;

// Key value table helpers
KeyValueTable MakeKeyValueTable(
    std::initializer_list<std::pair<std::string_view, std::string_view>> items);
void AddToKeyValueTable(
    KeyValueTable& keyValueTable,
    std::string_view key,
    std::string_view value);

struct DataResults {
    // Always sort the columns so data results are consistent.
    StringArray sortedColumnNames;
    // Each item in dataTable is a set of rows with a set of
    // data columns corresponding to the keys in sortedColumnNames.
    StringTable dataTable;
};

struct DataResultsWithCount {
    DataResults dataResults;
    int64_t totalCount;
};

inline int IndexOfColumn(const DataResults& dataResults, std::string_view columnName) {
    for (int i = 0; i < dataResults.sortedColumnNames.size(); ++i) {
        if (dataResults.sortedColumnNames[i] == columnName) {
            return i;
        }
    }
    return -1;
}

DataResults MakeDataResults(
    const StringArray& columnNames, const StringTable& dataTable);

DataResults MakeDataResultsStringView(
    const StringViewArray& columnNames, const StringTable& dataTable);

DataResults MakeDataResults(const KeyValueTable& keyValueTable);

DataResults MakeDataResultsFromKeyValueTableArray(
    const KeyValueTableArray& keyValueTableArray);

std::string GetDataResultsValue(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index = 0);

int GetDataResultsValueAsInt(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index = 0);

int64_t GetDataResultsValueAsInt64(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index = 0);

bool GetDataResultsValueAsBool(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index = 0);

bool GetDataResultsValueIsEmpty(
    const DataResults& dataResults,
    std::string_view columnName,
    size_t index = 0);

template <
    class ColumnArrayType, 
    class ColumnArrayAccessor, 
    class ColumnArraySizeAccessor,
    class DataTableType, 
    class DataTableAccessor,
    class DataTableSizeAccessor>
DataResults MakeDataResultsParamterized(
    const ColumnArrayType& columnArray,
    ColumnArrayAccessor columnArrayAccessor,
    ColumnArraySizeAccessor columnArraySizeAccessor,
    const DataTableType& dataTable,
    DataTableAccessor dataTableAccessor,
    DataTableSizeAccessor dataTableSizeAccessor) {
    DataResults dataResults;
    std::map<std::string, size_t> sortedColumns;
    for (size_t i = 0; i < columnArraySizeAccessor(columnArray); ++i) {
        sortedColumns.insert({ columnArrayAccessor(columnArray, i), i });
    }
    for (const auto& [columnName, index] : sortedColumns) {
        dataResults.sortedColumnNames.push_back(columnName);
    }
    for (size_t i = 0; i < dataTableSizeAccessor(dataTable); ++i) {
        StringArray row;
        for (const auto& [columnName, index] : sortedColumns) {
            row.push_back(dataTableAccessor(dataTable, i, index));
        }
        dataResults.dataTable.push_back(std::move(row));
    }
    return dataResults;
}

KeyValueTableArray KeyValueTableArrayFromDataResults(
    const DataResults& dataResults);

StringArray StringArrayFromDataResultsColumn(
    const DataResults& dataResults, size_t columnIndex = 0);

StringArray StringArrayFromKeyValueTableArrayColumn(
    const KeyValueTableArray& keyValueTableArray, size_t columnIndex = 0);

// TODO(mason) - finish wiring this up.
struct StringValueOrBlob {
    bool IsString() const { return !stringVal.empty(); }
    bool IsBlob() const { return !blob.empty(); }
    void SetString(std::string_view str) { 
        blob.clear(); stringVal = str.data(); 
    }
    void SetBlob(const Blob& blob) { stringVal.clear(); this->blob = blob; }
    std::string stringVal;
    Blob blob;
};
using KeyValueTableWithBlob = std::map<std::string, StringValueOrBlob>;

// Convert string array into a hash table set for lookup
StringSet StringSetFromStringArray(const StringArray& stringArray);

// Add items to string set
void AddStringArrayToSet(StringSet& stringSet, const StringArray& stringArray);

// Returns true if set contains the given string
bool SetContains(const StringSet& stringSet, std::string_view value);

// Take a string array and returns a comma / space separated single string
std::string StringArrayToCommaSeparatedString(const StringArray& stringArray);

// Converts an integer to a string
std::string StringFromInt(int n);
std::string StringFromInt(int64_t n);
// On Linux/GCC, int64_t is `long` but `long long` is a distinct type.
// This overload prevents ambiguous-call errors from GCC.
// On MSVC, int64_t is already `long long` so this would be a redefinition.
#ifndef _MSC_VER
std::string StringFromInt(long long n);
#endif

std::string StringToLower(std::string_view str);

// Returns a copy of `str` with leading and trailing ASCII whitespace
// (space, tab, \n, \r, \v, \f) removed. The interior is not modified.
std::string StringTrim(std::string_view str);

bool StringToBool(std::string_view str);

// Takes a string like "{replace} {me}" and a KeyValueTable like
// { {"replace", "hello"}, {"me", "world"} } and returns "hello world".
std::string FormatString(
    std::string_view format,
    const KeyValueTable& keyValueTable);

std::string URLEncode(std::string_view str);
std::string URLDecode(std::string_view str);

std::pair<std::string_view, std::string_view> SplitString(
    std::string_view strToBeSplit, std::string_view delimiter);

// Normalizes all \n without a preceding \r to be \r\n
std::string NormalizeCrLf(std::string_view str);

// Implementation detail for ContainerContains
namespace detail {

    template <class C, class T>
    auto has_find_impl(int) -> decltype(
        std::declval<const C&>().find(std::declval<T>()), std::true_type{});

    template <class C, class T>
    auto has_find_impl(...) -> std::false_type;

    template <class C, class T>
    inline constexpr bool has_find_v = decltype(has_find_impl<C, T>(0))::value;

} // namespace detail {

template <class Container, class Type>
bool ContainerContains(const Container& cont, const Type& value)
{
    if constexpr (detail::has_find_v<Container, const Type&>) {
        return cont.find(value) != cont.end();
    }
    else {
        return std::find(
            std::begin(cont), std::end(cont), value) != std::end(cont);
    }
}

// Implementation details for FNV-1a hash functions
namespace detail {
// 64-bit FNV-1a constants
static constexpr std::uint64_t kFnvOffsetBasis64 = 14695981039346656037ull;
static constexpr std::uint64_t kFnvPrime64 = 1099511628211ull;
} // namespace detail {

// Start value for a new hash (or use your own seed if you want to shard namespaces)
constexpr std::uint64_t HashInit(std::uint64_t seed = 0) noexcept {
    // Mix the user seed into the FNV offset basis
    return detail::kFnvOffsetBasis64 ^ (seed + 0x9e3779b97f4a7c15ull);
}

inline std::uint64_t HashAddBytes(std::uint64_t h, const void* data, std::size_t n) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= detail::kFnvPrime64;
    }
    return h;
}

inline std::uint64_t HashAdd(std::uint64_t h, std::string_view s) noexcept {
    return HashAddBytes(h, s.data(), s.size());
}

inline std::uint64_t HashAdd(std::uint64_t h, std::uint64_t x) noexcept {
    // Hash integer in a fixed byte order (little-endian here) to be stable cross-platform.
    std::uint8_t b[8];
    b[0] = static_cast<std::uint8_t>(x);
    b[1] = static_cast<std::uint8_t>(x >> 8);
    b[2] = static_cast<std::uint8_t>(x >> 16);
    b[3] = static_cast<std::uint8_t>(x >> 24);
    b[4] = static_cast<std::uint8_t>(x >> 32);
    b[5] = static_cast<std::uint8_t>(x >> 40);
    b[6] = static_cast<std::uint8_t>(x >> 48);
    b[7] = static_cast<std::uint8_t>(x >> 56);
    return HashAddBytes(h, b, 8);
}

inline std::uint64_t HashAdd(std::uint64_t h, std::int64_t x) noexcept {
    return HashAdd(h, static_cast<std::uint64_t>(x));
}

inline std::uint64_t HashAdd(std::uint64_t h, int x) noexcept {
    return HashAdd(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)));
}
