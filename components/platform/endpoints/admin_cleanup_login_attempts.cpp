#include "endpoints/admin_cleanup_login_attempts.h"

#include "business_logic/scheduled_jobs/platform_jobs.h"
#include "endpoints/endpoint_auth_helper.h"
#include "endpoints/scheduled_job_handler.h"

namespace Endpoints {
namespace {

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/admin/cleanup_login_attempts")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleScheduledJobPost(webApp, req, resp, &PostAdminCleanupLoginAttempts);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value PostAdminCleanupLoginAttempts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {
    return PostScheduledJob(endpointAuthHelper, resp, PlatformJobs::kJobCleanupLoginAttempts,
                            &PlatformJobs::RunPlatformJob);
}

}  // namespace Endpoints
