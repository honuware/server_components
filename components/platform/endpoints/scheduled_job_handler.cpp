#include "endpoints/scheduled_job_handler.h"

#include <string>

#include "db_schema/permissions.h"
#include "endpoints/endpoint_auth_helper.h"
#include "util/error_response.h"
#include "util/logging.h"

namespace Endpoints {

void HandleScheduledJobPost(WebApp* webApp, const crow::request& req, crow::response& resp,
                            ScheduledJobPost post) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = post(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            const std::string counts = result.ToString();
            // The scheduler logs only the status, so the counts are logged here.
            LogInfo() << "[scheduled_job] event=job_ran path=" << req.url << " result=" << counts << "\n";
            resp.set_header("Content-Type", "application/json");
            resp.write(counts);
        }
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

Json::Value PostScheduledJob(EndpointAuthHelper& endpointAuthHelper, crow::response& resp,
                             std::string_view jobName, ScheduledJobRunner runner) {
    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    Json::Value result;
    tp->RunInTransaction([&](Transaction& transaction) {
        if (!endpointAuthHelper.RequirePermission(
                transaction, DbSchema::kPermissionRunScheduledJobs, resp)) {
            return;
        }
        std::optional<Json::Value> counts = runner(
            transaction, endpointAuthHelper.GetDatabaseHelper(), *endpointAuthHelper.GetSecretsHelper(),
            endpointAuthHelper.GetMailHelper(), jobName);
        if (!counts) {
            resp = ErrorResponse::InternalError("Unknown scheduled job: " + std::string(jobName));
            return;
        }
        result = *counts;
        resp.code = 200;
    });
    return result;
}

}  // namespace Endpoints
