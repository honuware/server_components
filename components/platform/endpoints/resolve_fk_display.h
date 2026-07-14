#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

    // Expected POST body:
    // {
    //   "parent_table_name": "...",
    //   "values": ["pk1", "pk2", ...]
    // }
    Json::Value ResolveFkDisplay(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message);

}  // namespace Endpoints
