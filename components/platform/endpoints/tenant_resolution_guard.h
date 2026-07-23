#pragma once

#include <chrono>
#include <memory>
#include <mutex>

#include <crow/http_request.h>
#include <crow/http_response.h>

#include "business_logic/tenancy/tenant_context.h"
#include "business_logic/tenancy/tenant_header.h"

namespace Tenancy {
    class TenantResolver;
}

namespace Endpoints {

// Request-edge tenant resolution (tenancy plan Phase 3.2 / 3.3).
//
// Runs as a Crow middleware so a bad request is rejected uniformly — every
// /api/* route inherits the behavior with zero per-endpoint edits, exactly like
// CloudFrontOriginGuard. On each request it reads the X-Honuware-Site header,
// asks the installed TenantResolver to resolve it, and:
//   - success -> passes through untouched. EndpointAuthHelper::Initialize()
//                independently resolves the same site key (the resolver caches)
//                and routes the request to that tenant's resources.
//   - failure -> writes a small JSON error and ends the response before any
//                handler runs:
//                  control mode -> 421 Misdirected Request, error one of
//                    "missing_site_header" / "unknown_site" / "site_suspended".
//                    NEVER falls through to a default tenant (that would
//                    cross-serve data).
//                  fixed mode   -> 400 Bad Request, error "site_mismatch"
//                    (a single-tenant server was sent a header for a different
//                    site; an empty header always resolves in fixed mode).
//
// When no resolver is installed (SetResolver never called) the guard is inert
// and passes every request through — the pre-tenancy single-tenant path and the
// default for any WebApp built without tenancy wiring.
//
// The /api/health prefix is always allow-listed: health probes carry no site
// header and must work in every mode against the deployment's global provider.
class TenantResolutionGuard {
public:
    struct context {
        bool resolved = false;
        Tenancy::TenantContext tenant;
    };

    // Wires the resolver + mode. Called (out-of-line) by WebApp::SetTenantResolver
    // so the composition root installs both in one step.
    void SetResolver(
        std::shared_ptr<Tenancy::TenantResolver> resolver,
        Tenancy::TenancyMode mode);

    std::shared_ptr<Tenancy::TenantResolver> GetResolver() const {
        return resolver_;
    }

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);

private:
    std::shared_ptr<Tenancy::TenantResolver> resolver_;
    Tenancy::TenancyMode mode_ = Tenancy::TenancyMode::Fixed;

    // Rate-limited rejection logging, mirroring CloudFrontOriginGuard so a flood
    // of misdirected requests can't spam the log.
    std::mutex logMutex_;
    std::chrono::steady_clock::time_point lastRejectLog_{};
};

}  // namespace Endpoints
