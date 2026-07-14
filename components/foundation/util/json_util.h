#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <crow/json.h>

#include "util/types.h"
#include "json_value.h"

namespace Json {

// Fetches the given string field from a JSON object
std::string GetStringField(
    const crow::json::rvalue& value, std::string_view key);

// Fetches the given integer field from a JSON object
int GetIntField(const crow::json::rvalue& value, std::string_view key);

// Converts a writable JSON object to a readable one
crow::json::rvalue RvalueFromWvalue(const crow::json::wvalue& value);

// Fetches the given string field from a JSON object.
// Note that this uses RvalueFromWvalue under the covers
// so it is more efficient to use that and the corresponding
// read accessors if you are reading more than one field.
std::string GetStringField(
    const crow::json::wvalue& value, std::string_view key);

// Fetches the given integer field from a JSON object.
// Note that this uses RvalueFromWvalue under the covers
// so it is more efficient to use that and the corresponding
// read accessors if you are reading more than one field.
int GetIntField(const crow::json::wvalue& value, std::string_view key);

// Loads a wvalue from a text string
crow::json::wvalue WvalueFromText(std::string_view jsonText);

// Compares two wvalues for equality
bool EqualJsonValues(
    const crow::json::wvalue& value1, const crow::json::wvalue& value2);
bool EqualJsonValues(
    const crow::json::rvalue& value1, const crow::json::rvalue& value2);

// Convert KeyValueTable to JSON
Value JsonFromKeyValueTable(const KeyValueTable& table);
Value JsonFromKeyValueTableArray(
    const KeyValueTableArray& keyValueTableArray);

// Convert JSON to KeyValueTable
KeyValueTable KeyValueTableFromJson(const Value& value);

std::string StringFromBool(bool value);

Value JsonFromStringArray(
    const StringArray& stringArray);

Value JsonFromArrayOfJson(
    const std::vector<Value>& jsonArray);

Value JsonFromDataResults(
    const DataResults& dataResults);

Value JsonFromDataResultsWithCount(
    const DataResultsWithCount& dataResultsWithCount);

}  // namespace Json {