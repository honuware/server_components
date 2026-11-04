#pragma once

#include <crow.h>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// POST /api/admin/notify_admin_alerts (H13) — mails the admin alerts recorded
// since the last digest to admin_alerts_recipient
// (PlatformJobs::SendAdminAlertsDigest). Gated on run_scheduled_jobs; the body
// is ignored. Responds {"alerts_count": n, "mail_sent": bool,
// "last_notified_us": n}; a failed send is a 500 and the next run retries.
Json::Value PostAdminNotifyAdminAlerts(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
