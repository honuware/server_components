#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    Json::Value GetDbSchema(EndpointAuthHelper& endpointAuthHelper);

}  // namespace Endpoints