#include "json_test_util.h"

#include <unordered_set>

#include "db_schema/people.h"


namespace JsonTestUtil {

Json::Value JsonPerson(std::string_view email,
    std::string_view first_name, std::string_view last_name,
    std::string_view password_hash) {
    Json::JsonObject person;
    person[std::string(DbSchema::kPeopleEmail)] = std::string(email);
    person[std::string(DbSchema::kPeopleFirstName)] = std::string(first_name);
    person[std::string(DbSchema::kPeopleLastName)] = std::string(last_name);
    person[std::string(DbSchema::kPeoplePasswordHash)] = std::string(password_hash);
    return Json::Value(std::move(person));
}

static int IndexOfJsonColumn(const Json::Value& cols, const std::string& name) {
    if (!cols.IsArray()) return -1;
    int idx = 0;
    for (const auto& c : cols.GetArray()) {
        if (c.Is<std::string>() && c.Get<std::string>() == name) return idx;
        ++idx;
    }
    return -1;
}

bool CompareJsonDataResultsMinusColumnsHelper(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    const std::vector<std::string_view>& columnsToRemove) {

    if (!dataResults1.HasChildren() || !dataResults2.HasChildren()) {
        if (!dataResults1.HasChildren()) std::cout << "dataResults1 has no children.\n";
        if (!dataResults2.HasChildren()) std::cout << "dataResults2 has no children.\n";
        return false;
    }

    const auto& dr1 = dataResults1.GetChildren();
    const auto& dr2 = dataResults2.GetChildren();

    auto itCols1 = dr1.find("sortedColumnNames");
    auto itCols2 = dr2.find("sortedColumnNames");
    auto itRows1 = dr1.find("dataTable");
    auto itRows2 = dr2.find("dataTable");
    if (itCols1 == dr1.end() || itCols2 == dr2.end() || itRows1 == dr1.end() || itRows2 == dr2.end()) {
        if (itCols1 == dr1.end()) std::cout << "dataResults1 missing sortedColumnNames.\n";
        if (itCols2 == dr2.end()) std::cout << "dataResults2 missing sortedColumnNames.\n";
        if (itRows1 == dr1.end()) std::cout << "dataResults1 missing dataTable.\n";
        if (itRows2 == dr2.end()) std::cout << "dataResults2 missing dataTable.\n";
        return false;
    }

    const Json::Value& cols1 = itCols1->second;
    const Json::Value& cols2 = itCols2->second;
    const Json::Value& rows1 = itRows1->second;
    const Json::Value& rows2 = itRows2->second;
    if (!cols1.IsArray() || !cols2.IsArray() || !rows1.IsArray() || !rows2.IsArray()) {
        if (!cols1.IsArray()) std::cout << "dataResults1 sortedColumnNames is not an array.\n";
        if (!cols2.IsArray()) std::cout << "dataResults2 sortedColumnNames is not an array.\n";
        if (!rows1.IsArray()) std::cout << "dataResults1 dataTable is not an array.\n";
        if (!rows2.IsArray()) std::cout << "dataResults2 dataTable is not an array.\n";
        return false;
    }

    // Build removal set
    std::unordered_set<std::string> removeSet;
    removeSet.reserve(columnsToRemove.size());
    for (auto sv : columnsToRemove) removeSet.emplace(sv);

    // Build kept set from dr1 columns
    std::unordered_set<std::string> keptCols1;
    keptCols1.reserve(cols1.GetArray().size());
    for (const Json::Value& c : cols1.GetArray()) {
        if (!c.Is<std::string>()) continue;
        std::string col = c.Get<std::string>();
        if (removeSet.find(col) == removeSet.end()) {
            keptCols1.insert(std::move(col));
        }
    }

    // Validate dr2 kept columns exist in keptCols1 and count matches
    size_t keptCount2 = 0;
    for (const Json::Value& c : cols2.GetArray()) {
        if (!c.Is<std::string>()) continue;
        std::string col = c.Get<std::string>();
        if (removeSet.find(col) != removeSet.end()) continue;
        if (keptCols1.find(col) == keptCols1.end()) {
            std::cout << "Column " << col << " found in dataResults2 but not in dataResults1 after removals.\n";
            return false;
        }
        ++keptCount2;
    }
    if (keptCount2 != keptCols1.size()) {
        std::cout << "Kept column count mismatch after removals: dataResults1 has "
            << keptCols1.size() << ", dataResults2 has " << keptCount2 << ".\n";
        return false;
    }

    // Row count must match
    if (rows1.GetArray().size() != rows2.GetArray().size()) {
        std::cout << "Row count mismatch: dataResults1 has "
            << rows1.GetArray().size() << ", dataResults2 has " << rows2.GetArray().size() << ".\n";
        return false;
    }

    // Compare each row's kept columns
    for (size_t r = 0; r < rows1.GetArray().size(); ++r) {
        const Json::Value& row1 = rows1[r];
        const Json::Value& row2 = rows2[r];
        if (!row1.IsArray() || !row2.IsArray()) {
            if (!row1.IsArray()) std::cout << "Row " << r << " in dataResults1 is not an array.\n";
            if (!row2.IsArray()) std::cout << "Row " << r << " in dataResults2 is not an array.\n";
            return false;
        }
        for (const auto& col : keptCols1) {
            int c1 = IndexOfJsonColumn(cols1, col);
            int c2 = IndexOfJsonColumn(cols2, col);
            if (c1 < 0 || c2 < 0) return false;
            if (static_cast<size_t>(c1) >= row1.GetArray().size() ||
                static_cast<size_t>(c2) >= row2.GetArray().size()) {
                std::cout << "Column index out of bounds for column " << col << ".\n";
                return false;
            }
            const Json::Value& v1 = row1[static_cast<size_t>(c1)];
            const Json::Value& v2 = row2[static_cast<size_t>(c2)];
            if (!v1.Is<std::string>() || !v2.Is<std::string>()) return false;
            if (v1.Get<std::string>() != v2.Get<std::string>()) {
                std::cout << "Data mismatch at row " << r << ", column " << col << ": "
                    << v1.Get<std::string>() << " != "
                    << v2.Get<std::string>() << ".\n";
                return false;
            }
        }
    }

    return true;
}

bool CompareJsonObjectMinusColumnsHelper(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    const std::vector<std::string_view>& fieldsToRemove) {

    if (!dataResults1.HasChildren() || !dataResults2.HasChildren()) {
        if (!dataResults1.HasChildren()) std::cout << "dataResults1 has no children.\n";
        if (!dataResults2.HasChildren()) std::cout << "dataResults2 has no children.\n";
        return false;
    }
    const auto& obj1 = dataResults1.GetChildren();
    const auto& obj2 = dataResults2.GetChildren();

    // Build removal set
    std::unordered_set<std::string> removeSet;
    removeSet.reserve(fieldsToRemove.size());
    for (auto sv : fieldsToRemove) removeSet.emplace(sv);

    // Build kept key set from obj1
    std::unordered_set<std::string> keptKeys1;
    for (const auto& [key, value] : obj1) {
        if (removeSet.find(key) == removeSet.end()) {
            keptKeys1.insert(key);
        }
    }

    // Ensure obj2 has the same kept keys
    size_t kept2Count = 0;
    for (const auto& [key, value] : obj2) {
        if (removeSet.find(key) != removeSet.end()) continue;
        if (keptKeys1.find(key) == keptKeys1.end()) {
            std::cout << "Field " << key << " found in dataResults2 but not in dataResults1 after removals.\n";
            return false;
        }
        ++kept2Count;
    }
    if (kept2Count != keptKeys1.size()) {
        std::cout << "Kept field count mismatch after removals: dataResults1 has "
            << keptKeys1.size() << ", dataResults2 has " << kept2Count << ".\n";
        return false;
    }

    // Compare values for each kept key
    for (const auto& key : keptKeys1) {
        auto it1 = obj1.find(key);
        auto it2 = obj2.find(key);
        if (it1 == obj1.end() || it2 == obj2.end()) return false;
        if (it1->second.ToString() != it2->second.ToString()) {
            std::cout << "Value mismatch for key " << key << ": "
                << it1->second.ToString() << " != "
                << it2->second.ToString() << ".\n";
            return false;
        }
    }

    return true;
}

}  // namespace JsonTestUtil