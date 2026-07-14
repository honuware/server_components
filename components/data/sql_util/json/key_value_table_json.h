#pragma once

#include "sql_util/database_common.h"
#include "util/json_value.h"

namespace SqlUtil {

// Convert a KeyValueTable to a JSON object.
// String values that look like integers are converted to int64_t.
Json::Value KeyValueTableToJson(const KeyValueTable& table);

// Convert a KeyValueTableArray to a JSON array of objects.
Json::Value KeyValueTableArrayToJson(const KeyValueTableArray& tableArray);

}  // namespace SqlUtil
