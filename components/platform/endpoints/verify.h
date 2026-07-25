#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"
#include "util/json_value.h"

namespace Endpoints {

// POST /api/verify with JSON body:
//   { "email": "...", "secret": "..." }
// Phase 3.3 of the security review: the previous URL-path form put the
// verification secret in the URL, where it would land in CloudFront /
// reverse-proxy access logs. The email link in the verification mail
// now points at the SPA, which immediately POSTs to this endpoint.
void Verify(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

}  // namespace Endpoints
