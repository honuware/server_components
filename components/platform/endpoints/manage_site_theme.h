#pragma once

#include <crow.h>
#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// GET /api/manage/site_theme — the full editable branding surface for the
// resolved tenant. Admin only.
//
// Tenant Theming Phase 6. This is the CURATED alternative to the generic CRUD
// surface, which `config_secrets` is deliberately excluded from (it holds live
// credentials — D1). Everything a studio can change about how its site looks
// arrives here in one call, and goes back through PUT.
//
// The response distinguishes **unset from default**, which the generic surface
// cannot: each field carries its stored `value` plus `is_set`. A blank field a
// tenant deliberately cleared and a field they never touched look identical in
// `config_secrets` (both read as ""), but mean different things to an editor —
// "reset to default" has to know which it is looking at.
//
// Font FAMILIES are not editable here: they are table rows, not secrets, and
// carry binaries. This endpoint serves the tenant's family list read-only so the
// role dropdowns have options, and the font manager that edits the inventory is
// its own surface.
Json::Value GetManageSiteTheme(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

// PUT /api/manage/site_theme — validated writes. Admin only.
//
// Body: `{ "content": { slot_key: value, … }, "theme": { token_key: value, … } }`
// Only the keys present are written, so a section-at-a-time save is safe.
//
// Validation is the SAME code the read path normalizes with (D10), so the
// editor cannot store something site_info would then silently drop. A rejected
// field returns 400 naming the field and why, rather than partially applying —
// a half-saved theme is worse than a refused one.
Json::Value PutManageSiteTheme(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp);

}  // namespace Endpoints
