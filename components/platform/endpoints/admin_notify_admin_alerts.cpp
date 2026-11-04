#include "endpoints/admin_notify_admin_alerts.h"

#include "business_logic/scheduled_jobs/platform_jobs.h"
#include "endpoints/endpoint_auth_helper.h"
#include "endpoints/scheduled_job_handler.h"

namespace Endpoints {
namespace {

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/admin/notify_admin_alerts")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleScheduledJobPost(webApp, req, resp, &PostAdminNotifyAdminAlerts);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value PostAdminNotifyAdminAlerts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {
    return PostScheduledJob(endpointAuthHelper, resp, PlatformJobs::kJobNotifyAdminAlerts,
                            &PlatformJobs::RunPlatformJob);
}

}  // namespace Endpoints
