#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    // Expected data like:
    // {
    //   table_name: {table_name},
    //   value: {object_data}
    // }
    Json::Value AddItemFetchPrimaryKey(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message);

}  // namespace Endpoints
