#pragma once

#include <crow.h>

#include "endpoints/endpoint_auth_helper.h"
#include "util/json_value.h"

namespace Endpoints {

// POST /api/forgot_password with JSON body `{ "email": "..." }`.
// Finishing website Phase 11.4. Mails a reset link when the address has an
// account (Auth::PasswordResetHelper::RequestReset). The answer is ALWAYS 200
// with the same empty body, account or not, and the mail is sent from the
// ThreadPool so the response time doesn't tell either. Too many requests for
// one address or from one IP get the generic 429.
void ForgotPassword(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    const Json::Value& message);

}  // namespace Endpoints
