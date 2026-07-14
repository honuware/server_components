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
    void AddItem(EndpointAuthHelper& endpointAuthHelper, const Json::Value& message);

}  // namespace Endpoints