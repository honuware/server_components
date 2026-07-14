#include "test_helper.h"

#include <unordered_set>
#include <set>
#include <iostream>

bool CompareDataResultsMinusColumnsHelper(
    const DataResults& dataResults1,
    const DataResults& dataResults2,
    const std::vector<std::string_view>& columnsToRemove) {

    // Build a set of columns to remove for quick lookup
    std::unordered_set<std::string> removeSet;
    removeSet.reserve(columnsToRemove.size());
    for (auto sv : columnsToRemove) removeSet.emplace(sv);

    // Build the set of "kept" columns from dataResults1 (not removed)
    std::unordered_set<std::string> keptCols1;
    keptCols1.reserve(dataResults1.sortedColumnNames.size());
    for (const auto& col : dataResults1.sortedColumnNames) {
        if (removeSet.find(col) == removeSet.end()) {
            keptCols1.insert(col);
        }
    }

    // Walk the columns in dataResults2 and ensure each non-removed column exists in keptCols1
    size_t keptCount2 = 0;
    for (const auto& col : dataResults2.sortedColumnNames) {
        if (removeSet.find(col) != removeSet.end()) continue;
        if (keptCols1.find(col) == keptCols1.end()) {
            return false;
        }
        ++keptCount2;
    }

    if (keptCount2 != keptCols1.size()) {
        return false;
    }

    // Row count must match
    if (dataResults1.dataTable.size() != dataResults2.dataTable.size()) {
        return false;
    }

    // Compare each row's kept columns
    for (size_t r = 0; r < dataResults1.dataTable.size(); ++r) {
        const auto& row1 = dataResults1.dataTable[r];
        const auto& row2 = dataResults2.dataTable[r];
        // Defensive: columns could differ in count; rely on IndexOfColumn for mapping
        for (const auto& col : keptCols1) {
            int c1 = IndexOfColumn(dataResults1, col);
            int c2 = IndexOfColumn(dataResults2, col);
            if (c1 < 0 || c2 < 0) return false;
            if (static_cast<size_t>(c1) >= row1.size() ||
                static_cast<size_t>(c2) >= row2.size()) {
                return false;
            }
            if (row1[static_cast<size_t>(c1)] != row2[static_cast<size_t>(c2)]) {
                return false;
            }
        }
    }

    return true;
}

bool CompareKeyValueTablesMinusColumnsHelper(
    const KeyValueTable& table1,
    const KeyValueTable& table2,
    const std::vector<std::string_view>& columnsToRemove) {
    std::set<std::string> keys1, keys2;
    for (const auto& [key, value] : table1) {
        if (!ContainerContains(columnsToRemove, key)) {
            keys1.insert(key);
        }   
    }
    for (const auto& [key, value] : table2) {
        if (!ContainerContains(columnsToRemove, key)) {
            keys2.insert(key);
        }
    }
    if(keys1 != keys2) {
        std::cout << "Key sets differ between tables.\n";
        return false;
    }
    for(const auto& key : keys1) {
        if (table1.at(key) != table2.at(key)) {
            std::cout << "Value mismatch for key " << key << ": "
                << table1.at(key) << " != " << table2.at(key) << ".\n";
            return false;
        }
    }
    return true;
}

std::uint64_t HashKeyValueTableMinusColumns(
    const KeyValueTable& table,
    const std::vector<std::string_view>& columnsToRemove) {
    std::uint64_t hash = HashInit();
    for (const auto& [key, value] : table) {
        if (!ContainerContains(columnsToRemove, key)) {
            hash = HashAddBytes(hash, key.data(), key.size());
            hash = HashAddBytes(hash, value.data(), value.size());
        }
    }
    return hash;
}

bool CompareKeyValueTableArraysMinusColumnsHelper(
    const KeyValueTableArray& tableArray1,
    const KeyValueTableArray& tableArray2,
    const std::vector<std::string_view>& columnsToRemove) {
    if(tableArray1.size() != tableArray2.size()) {
        std::cout << "Table array size mismatch: "
            << tableArray1.size() << " != " << tableArray2.size() << ".\n";
        return false;
    }
    std::unordered_map<std::uint64_t, const KeyValueTable*> hashArray1, hashArray2;
    for (const auto& table : tableArray1) {
        std::uint64_t hash = HashKeyValueTableMinusColumns(table, columnsToRemove);
        hashArray1.emplace(hash, &table);
    }
    for (const auto& table : tableArray2) {
        std::uint64_t hash = HashKeyValueTableMinusColumns(table, columnsToRemove);
        hashArray2.emplace(hash, &table);
    }
    if(hashArray1.size() != hashArray2.size()) {
        std::cout << "Hashed table array size mismatch: "
            << hashArray1.size() << " != " << hashArray2.size() << ".\n";
        return false;
    }
    for (const auto& [hash, table] : hashArray1) {
        auto iter2 = hashArray2.find(hash);
        if (iter2 == hashArray2.end()) {
            std::cout << "Hash " << hash << " from tableArray1 not found in tableArray2.\n";
            return false;
        }
        if (!CompareKeyValueTablesMinusColumnsHelper(
                *table, *(iter2->second), columnsToRemove)) {
            std::cout << "Tables hashed to the same value but differ in content.\n";
            return false;
        }
    }
    return true;
}


