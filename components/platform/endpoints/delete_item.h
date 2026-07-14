#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    // Use /delete_item/{tableName}/{columnName}/{value}
    void DeleteItem(
        EndpointAuthHelper& endpointAuthHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view value);

}  // namespace Endpoints