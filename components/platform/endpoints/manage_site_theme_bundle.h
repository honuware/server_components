#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Tenant Theming Phase 9 (OQ-TF1) — download and upload a whole look.
//
// Three routes, all admin-only:
//
//   GET  /api/manage/site_theme_bundle           -> application/zip
//   POST /api/manage/site_theme_bundle/validate  -> the report, WITHOUT writing
//   POST /api/manage/site_theme_bundle           -> apply, returns the report
//
// The validate route is what makes trying alternatives safe: the studio sees
// what a theme will change — and what it could not read — before anything
// changes. The apply route runs the same code with dryRun off.
//
// Query flags on both POSTs:
//   ?lenient=1  apply what is understood and report what was skipped
//               (default is strict: refuse, naming the unknown keys)
//   ?merge=1    apply only what the bundle mentions, leaving the rest alone
//               (default is replace: absent means back to the default)

// Binary body, so this returns void like the other file-serving endpoints.
void GetManageSiteThemeBundle(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

Json::Value PostManageSiteThemeBundleValidate(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

Json::Value PostManageSiteThemeBundle(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
