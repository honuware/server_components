#pragma once

#include <string>
#include <string_view>

#include "util/json_value.h"

#include "test_helper.h"

namespace JsonTestUtil {

Json::Value JsonPerson(
    std::string_view email,
    std::string_view first_name,
    std::string_view last_name,
    std::string_view password_hash);

bool CompareJsonDataResultsMinusColumnsHelper(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    const std::vector<std::string_view>& columnsToRemove);

// Example use inside your variadic API:
template <class... Args>
bool CompareJsonDataResultsMinusColumns(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    Args&&... args) {
    auto views = CollectStrings(std::forward<Args>(args)...);
    return CompareJsonDataResultsMinusColumnsHelper(
        dataResults1, dataResults2, views);
}

bool CompareJsonObjectMinusColumnsHelper(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    const std::vector<std::string_view>& fieldsToRemove);

// Example use inside your variadic API:
template <class... Args>
bool CompareJsonObjectMinusColumns(
    const Json::Value& dataResults1,
    const Json::Value& dataResults2,
    Args&&... args) {
    auto views = CollectStrings(std::forward<Args>(args)...);
    return CompareJsonObjectMinusColumnsHelper(
        dataResults1, dataResults2, views);
}

}  // namespace JsonTestUtil {