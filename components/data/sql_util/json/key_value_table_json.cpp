#include "key_value_table_json.h"

#include <charconv>

namespace SqlUtil {

namespace {

// Check if a string represents an integer
bool IsIntegerString(const std::string& str) {
    if (str.empty()) return false;

    size_t start = 0;
    if (str[0] == '-') {
        if (str.size() == 1) return false;
        start = 1;
    }

    for (size_t i = start; i < str.size(); ++i) {
        if (str[i] < '0' || str[i] > '9') {
            return false;
        }
    }
    return true;
}

}  // namespace

Json::Value KeyValueTableToJson(const KeyValueTable& table) {
    Json::JsonObject obj;
    for (const auto& [key, value] : table) {
        // Try to convert to integer if it looks like one
        if (IsIntegerString(value)) {
            int64_t intValue = 0;
            auto [ptr, ec] = std::from_chars(
                value.data(), value.data() + value.size(), intValue);
            if (ec == std::errc{} && ptr == value.data() + value.size()) {
                obj[key] = intValue;
                continue;
            }
        }
        obj[key] = value;
    }
    return Json::Value(obj);
}

Json::Value KeyValueTableArrayToJson(const KeyValueTableArray& tableArray) {
    Json::JsonArray arr;
    arr.reserve(tableArray.size());
    for (const auto& table : tableArray) {
        arr.push_back(KeyValueTableToJson(table));
    }
    return Json::Value(arr);
}

}  // namespace SqlUtil
