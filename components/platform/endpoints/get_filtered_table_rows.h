#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

    // Expected POST body:
    // {
    //   "table_name": "...",
    //   "column_name": "sort column",
    //   "asc": true/false,
    //   "page_size": int,
    //   "page": int,
    //   "filter_pairs": [
    //     { "column_name": "...", "column_value": "..." }
    //   ]
    // }
    Json::Value GetFilteredTableRows(
        EndpointAuthHelper& endpointAuthHelper,
        const Json::Value& message);

}  // namespace Endpoints
