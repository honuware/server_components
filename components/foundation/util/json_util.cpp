#include "json_util.h"

#include <exception>

namespace Json {

std::string GetStringField(
    const crow::json::rvalue& value, std::string_view key) {
    return std::string(value[std::string(key)].s());
}

int GetIntField(const crow::json::rvalue& value, std::string_view key) {
    return value[std::string(key)].i();
}

crow::json::rvalue RvalueFromWvalue(const crow::json::wvalue& value) {
    crow::json::rvalue rvalueConvert = crow::json::load(value.dump());
    return rvalueConvert;
}

std::string GetStringField(
    const crow::json::wvalue& value, std::string_view key) {
    crow::json::rvalue rvalueConvert = RvalueFromWvalue(value);
    return GetStringField(rvalueConvert, key);
}

int GetIntField(const crow::json::wvalue& value, std::string_view key) {
    crow::json::rvalue rvalueConvert = RvalueFromWvalue(value);
    return GetIntField(rvalueConvert, key);
}

namespace {

    bool EqualJsonLists(
        const crow::json::rvalue& value1, const crow::json::rvalue& value2) {
        if (value1.t() != crow::json::type::List
            || value2.t() != crow::json::type::List) {
            return false;
        }
        if (value1.size() != value2.size()) {
            return false;
        }
        for (size_t i = 0; i < value1.size(); ++i) {
            if (!EqualJsonValues(value1[i], value2[i])) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> GetKeys(const crow::json::rvalue& value) {
        // For some weird reason, this method isn't const
        return const_cast<crow::json::rvalue&>(value).keys();
    }

    bool EqualJsonObjects(
        const crow::json::rvalue& value1, const crow::json::rvalue& value2) {
        if (value1.t() != crow::json::type::Object
            || value2.t() != crow::json::type::Object) {
            return false;
        }
        if (value1.size() != value2.size()) {
            return false;
        }
        // The keys don't need to be in order so sort them
        auto keys1 = GetKeys(value1);
        auto keys2 = GetKeys(value2);
        // Sort these things
        std::sort(keys1.begin(), keys1.end());
        std::sort(keys2.begin(), keys2.end());
        if (!std::equal(keys1.begin(), keys1.end(), keys2.begin())) {
            return false;
        }
        // Now compare the values
        for (const auto& key : keys1) {
            if (!EqualJsonValues(value1[key], value2[key])) {
                return false;
            }
        }
        return true;
    }

    bool EqualJsonSimpleValues(
        const crow::json::rvalue& value1, const crow::json::rvalue& value2) {
        if (value1.t() != value2.t()) {
            return false;
        }
        if (value1.t() == crow::json::type::Null
            || value1.t() == crow::json::type::True
            || value1.t() == crow::json::type::False) {
            return true;
        }
        // Convert both to strings
        return static_cast<std::string>(value1)
            == static_cast<std::string>(value2);
    }

}

crow::json::wvalue WvalueFromText(std::string_view jsonText) {
    crow::json::rvalue rval = crow::json::load(std::string(jsonText));
    return rval;
}

bool EqualJsonValues(
    const crow::json::wvalue& value1, const crow::json::wvalue& value2) {
    return EqualJsonValues(RvalueFromWvalue(value1), RvalueFromWvalue(value2));
}

bool EqualJsonValues(
    const crow::json::rvalue& value1, const crow::json::rvalue& value2) {
    if (value1.t() != value2.t()) {
        return false;
    }
    switch (value1.t()) {
    case crow::json::type::Null:
    case crow::json::type::False:
    case crow::json::type::True:
    case crow::json::type::Number:
    case crow::json::type::String:
        return EqualJsonSimpleValues(value1, value2);
    case crow::json::type::List:
        return EqualJsonLists(value1, value2);
    case crow::json::type::Object:
        return EqualJsonObjects(value1, value2);
    default:
        return false;
    }
}

Value JsonFromKeyValueTable(const KeyValueTable& table) {
    Value json;
    for (const auto& [key, value] : table) {
        json[key] = value;
    }
    return json;
}

Value JsonFromKeyValueTableArray(
    const KeyValueTableArray& keyValueTableArray) {
    std::vector<Json::Value> jsonArray;
    for (const KeyValueTable& table : keyValueTableArray) {
        jsonArray.push_back(JsonFromKeyValueTable(table));
    }
    return jsonArray;
}

// Convert JSON to StringTable
KeyValueTable KeyValueTableFromJson(const Value& value) {
    KeyValueTable table;
    if (value.HasChildren()) {
        for(const auto& [key, val] : value.GetChildren()) {
            table[key] = val.ToSimpleString();
        }
    }
    return table;
}

std::string StringFromBool(bool value) {
    return value ? "t" : "f";
}

Value JsonFromStringArray(
    const StringArray& stringArray) {
    std::vector<Value> jsonArray;
    for (const std::string& value : stringArray) {
        jsonArray.push_back(value);
    }
    return jsonArray;
}

Value JsonFromArrayOfJson(
    const std::vector<Value>& jsonArray) {
    std::vector<Value> result;
    for (const auto& item : jsonArray) {
        result.push_back(item);
    }
    return result;
}

Value JsonFromDataResults(
    const DataResults& dataResults) {
    Value jsonValue;
    jsonValue["sortedColumnNames"]
        = JsonFromStringArray(dataResults.sortedColumnNames);
    std::vector<Value> jsonArray;
    for (const StringArray& row : dataResults.dataTable) {
        jsonArray.push_back(JsonFromStringArray(row));
    }
    jsonValue["dataTable"] = JsonFromArrayOfJson(jsonArray);
    return jsonValue;
}

Value JsonFromDataResultsWithCount(
    const DataResultsWithCount& dataResultsWithCount) {
    Value jsonValue;
    jsonValue["sortedColumnNames"]
        = JsonFromStringArray(dataResultsWithCount.dataResults.sortedColumnNames);
    std::vector<Value> jsonArray;
    for (const StringArray& row : dataResultsWithCount.dataResults.dataTable) {
        jsonArray.push_back(JsonFromStringArray(row));
    }
    jsonValue["dataTable"] = JsonFromArrayOfJson(jsonArray);
    jsonValue["totalCount"] = dataResultsWithCount.totalCount;
    return jsonValue;
}

}  // namespace Json {