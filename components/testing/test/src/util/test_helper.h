#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <utility>

#include <util/types.h>

template <class... Args>
std::vector<std::string_view> CollectStrings(Args&&... args) {
    static_assert((std::is_convertible_v<Args, std::string_view> && ...),
        "All arguments must be convertible to std::string_view");

    std::vector<std::string_view> v;
    v.reserve(sizeof...(Args));
    (v.emplace_back(std::forward<Args>(args)), ...); // no copies, just views
    return v;
}

bool CompareDataResultsMinusColumnsHelper(
    const DataResults& dataResults1,
    const DataResults& dataResults2,
    const std::vector<std::string_view>& columnsToRemove);

// Example use inside your variadic API:
template <class... Args>
bool CompareDataResultsMinusColumns(
    const DataResults& dataResults1,
    const DataResults& dataResults2,
    Args&&... args) {
    auto views = CollectStrings(std::forward<Args>(args)...);
    return CompareDataResultsMinusColumnsHelper(
        dataResults1, dataResults2, views);
}

bool CompareKeyValueTablesMinusColumnsHelper(
    const KeyValueTable& table1,
    const KeyValueTable& table2,
    const std::vector<std::string_view>& columnsToRemove);

template <class... Args>
bool CompareKeyValueTablesMinusColumns(
    const KeyValueTable& table1,
    const KeyValueTable& table2,
    Args&&... args) {
    auto views = CollectStrings(std::forward<Args>(args)...);
    return CompareKeyValueTablesMinusColumnsHelper(
        table1, table2, views);
}

// Note that the key value tables can be in any order in the arrays
bool CompareKeyValueTableArraysMinusColumnsHelper(
    const KeyValueTableArray& tableArray1,
    const KeyValueTableArray& tableArray2,
    const std::vector<std::string_view>& columnsToRemove);

// Note that the key value tables can be in any order in the arrays
template <class... Args>
bool CompareKeyValueTableArraysMinusColumns(
    const KeyValueTableArray& tableArray1,
    const KeyValueTableArray& tableArray2,
    Args&&... args) {
    auto views = CollectStrings(std::forward<Args>(args)...);
    return CompareKeyValueTableArraysMinusColumnsHelper(
        tableArray1, tableArray2, views);
}
