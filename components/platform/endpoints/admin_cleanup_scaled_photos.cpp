#include "endpoints/admin_cleanup_scaled_photos.h"

#include "business_logic/scheduled_jobs/platform_jobs.h"
#include "endpoints/endpoint_auth_helper.h"
#include "endpoints/scheduled_job_handler.h"

namespace Endpoints {
namespace {

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/admin/cleanup_scaled_photos")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleScheduledJobPost(webApp, req, resp, &PostAdminCleanupScaledPhotos);
                });
    }
} g_setupRouting;

}  // namespace

Json::Value PostAdminCleanupScaledPhotos(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {
    return PostScheduledJob(endpointAuthHelper, resp, PlatformJobs::kJobCleanupScaledPhotos,
                            &PlatformJobs::RunPlatformJob);
}

}  // namespace Endpoints
