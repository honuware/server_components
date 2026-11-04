#pragma once

#include <optional>
#include <string_view>

#include <crow.h>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "util/json_value.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secrets_helper.h"

class EndpointAuthHelper;
class WebApp;

namespace Endpoints {

// H13 — what every scheduled-job endpoint shares, the framework's and an
// app's alike. The scheduler POSTs an empty body to /api/admin/<job> as the
// scheduler service account; an administrator can do the same by hand. Each
// job checks run_scheduled_jobs before anything else, runs in one
// transaction, and answers 200 with its counts. A job decides for itself
// whether there is work, so calling it again is safe.

using ScheduledJobPost = Json::Value (*)(EndpointAuthHelper&, const crow::request&, crow::response&);

// Runs the named job in the caller's transaction and returns its counts, or
// nullopt for a name it doesn't know. PlatformJobs::RunPlatformJob is the
// framework's; an app passes its own for its own jobs.
using ScheduledJobRunner = std::optional<Json::Value> (*)(
    Transaction&, DatabaseHelper, Secrets::SecretsHelper&, const ::Mail::MailHelperPtr&, std::string_view);

// The route handler every job TU registers: runs `post`, writes its JSON on a
// 200 (and logs it: the scheduler's own log has only the status), and turns
// an exception into a 500 (the transaction rolls back).
void HandleScheduledJobPost(WebApp* webApp, const crow::request& req, crow::response& resp,
                            ScheduledJobPost post);

// The body of every job endpoint: 401 signed out, 403 without
// run_scheduled_jobs, 500 without a database or for a name `runner` doesn't
// know; otherwise runs the job in one transaction, sets resp.code = 200 and
// returns its counts.
Json::Value PostScheduledJob(EndpointAuthHelper& endpointAuthHelper, crow::response& resp,
                             std::string_view jobName, ScheduledJobRunner runner);

}  // namespace Endpoints
