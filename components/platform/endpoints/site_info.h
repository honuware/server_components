#pragma once

#include <string>
#include <string_view>

#include "business_logic/branding/site_font_inventory.h"
#include "util/json_value.h"
#include "util/types.h"  // KeyValueTable

class EndpointAuthHelper;

namespace Endpoints {

// Pure: builds the JSON body for GET /api/site_info from the three top-level
// branding fields plus the two per-tenant maps. Kept separate from the I/O path
// (mirroring health's BuildHealthResponse) so the field mapping is
// unit-testable without a database.
//
// `content` and `theme` are always emitted as objects — empty ones if the tenant
// has set nothing — so the SPA can rely on the payload's shape (Tenant Theming
// decision D2: extend this response, never add a second bootstrap call). Every
// value is written as a JSON string; no numeric coercion, because a headline
// that happens to read "2026" is still a headline.
// `appBlocks` carries whatever the APP registered through
// Branding::RegisterSiteInfoBlock, and lands under a top-level `app` key —
// framework fields stay at the root, app fields are namespaced, and which is
// which is legible from the payload alone. Empty for a tenant/app that
// registered nothing, in which case `app` is an empty object rather than
// absent, so a client never has to distinguish "no blocks" from "old server".
Json::Value BuildSiteInfoResponse(
    std::string_view displayName,
    std::string_view websiteUrl,
    std::string_view logoUrl,
    const KeyValueTable& content,
    const KeyValueTable& theme,
    const Branding::SiteFontInventory& fonts,
    const Json::JsonObject& appBlocks = {});

// HTTP handler for `GET /api/site_info` (tenancy plan Phase 7.1, extended by
// Tenant Theming Phase 1). Returns the resolved tenant's PUBLIC branding —
// `display_name`, `website_url`, `logo_url`, plus the `content` slot map
// (Branding::LoadSiteContent) and the `theme` token map — sourced from that
// tenant's config secrets (`::Mail::LoadTenantBranding` for the studio name +
// website URL; `Secrets::kSiteLogoUrl` for the logo, empty by default). It
// powers runtime branding of the shared SPA bundle under Q10's multi-tenant
// strategy.
//
// `theme` is deliberately empty until Phase 4 wires the `site_theme_*` tokens:
// shipping the (empty) object now means the SPA's boot applier can be written
// against a payload shape that will not change under it.
//
// Unauthenticated by design: the SPA fetches it before login. It still resolves
// the tenant (EndpointAuthHelper::Initialize), so under the control resolver each
// site gets its own branding; under the fixed resolver a single-tenant / headerless
// consumer gets its static branding. Exposed as a free function so
// RegisterFrameworkEndpoints can anchor this translation unit into the link.
Json::Value GetSiteInfo(EndpointAuthHelper& endpointAuthHelper);

}  // namespace Endpoints
