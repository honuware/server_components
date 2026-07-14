#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

    // Expected POST body:
    // {
    //   "table_name": "...",
    //   "filter_pairs": [
    //     { "column_name": "...", "column_value": "..." }
    //   ]
    // }
    Json::Value GetRowByValues(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message);

}  // namespace Endpoints
