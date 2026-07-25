#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"

namespace Endpoints {

// Use /api/account_activation/{email}/{base64_encoded_activation_token}
void AccountActivation(
    EndpointAuthHelper& endpointAuthHelper,
    std::string_view email,
    std::string_view base64EncodedActivationToken);

}  // namespace Endpoints
