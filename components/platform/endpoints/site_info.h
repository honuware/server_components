#pragma once

#include <string>
#include <string_view>

#include "util/json_value.h"

class EndpointAuthHelper;

namespace Endpoints {

// Pure: builds the JSON body for GET /api/site_info from the three branding
// fields. Kept separate from the I/O path (mirroring health's
// BuildHealthResponse) so the field mapping is unit-testable without a database.
Json::Value BuildSiteInfoResponse(
    std::string_view displayName,
    std::string_view websiteUrl,
    std::string_view logoUrl);

// HTTP handler for `GET /api/site_info` (tenancy plan Phase 7.1). Returns the
// resolved tenant's PUBLIC branding — `display_name`, `website_url`, `logo_url`
// — sourced from that tenant's config secrets (`::Mail::LoadTenantBranding` for
// the studio name + website URL; `Secrets::kSiteLogoUrl` for the logo, empty by
// default). It powers runtime branding of the shared SPA bundle under Q10's
// multi-tenant strategy.
//
// Unauthenticated by design: the SPA fetches it before login. It still resolves
// the tenant (EndpointAuthHelper::Initialize), so under the control resolver each
// site gets its own branding; under the fixed resolver a single-tenant / headerless
// consumer gets its static branding. Exposed as a free function so
// RegisterFrameworkEndpoints can anchor this translation unit into the link.
Json::Value GetSiteInfo(EndpointAuthHelper& endpointAuthHelper);

}  // namespace Endpoints
