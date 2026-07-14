#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    Json::Value GetRowsByColumn(
        EndpointAuthHelper& endpointAuthHelper,
        const std::string& tableName,
        const std::string& columnName,
        bool asc,
        int pageSize,
        int page);

}  // namespace Endpoints