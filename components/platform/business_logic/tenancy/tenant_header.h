#pragma once

#include <string_view>

namespace Tenancy {

// The HTTP header CloudFront injects to tell the server which site (tenant) a
// request belongs to. Read at the request edge by TenantResolutionGuard. One
// documented constant so the header name lives in exactly one place (tenancy
// plan Phase 3.1, Q2).
inline constexpr std::string_view kSiteHeaderName = "X-Honuware-Site";

// How a deployment resolves tenants, chosen by the app composition root via the
// HONUWARE_TENANT_MODE env var (tenancy plan Phase 3.1):
//   Fixed   — the zero-ceremony default: one configured tenant, no control
//             database. Dev, local runs, and single-tenant honuware consumers
//             (e.g. CommunityFinder) need no site header and no new env vars.
//   Control — reads the control database's `tenants` table and multiplexes many
//             CloudFront-fronted sites off one binary.
enum class TenancyMode {
    Fixed,
    Control,
};

}  // namespace Tenancy
