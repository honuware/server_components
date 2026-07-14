#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

    // Expected POST body:
    // {
    //   "table_name": "...",
    //   "search_text": "...",
    //   "page_size": int
    // }
    Json::Value GetFkOptions(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message);

}  // namespace Endpoints
