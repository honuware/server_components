#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    Json::Value GetTableRows(
        EndpointAuthHelper& endpointAuthHelper,
        const std::string& tableName);

}  // namespace Endpoints