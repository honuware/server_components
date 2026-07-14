#pragma once

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

Json::Value GetRow(
    EndpointAuthHelper& endpointAuthHelper,
    const std::string& tableName,
    const std::string& columnName,
    const std::string& value);

} // namespace Endpoints