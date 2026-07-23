#include "tenant_resolution_guard.h"

#include <optional>
#include <string>
#include <string_view>

#include <crow/common.h>

#include "business_logic/tenancy/tenant_resolver.h"
#include "db_schema/tenants.h"
#include "util/logging.h"

namespace Endpoints {
namespace {

// Health probes carry no site header and must pass in every mode. Full URL
// prefix match, mirroring CloudFrontOriginGuard's allow-list.
constexpr std::string_view kHealthPathPrefix = "/api/health";

constexpr std::chrono::minutes kRejectLogThrottle{1};

bool IsHealthPath(const std::string& url) {
    return url.size() >= kHealthPathPrefix.size() &&
           url.compare(0, kHealthPathPrefix.size(), kHealthPathPrefix) == 0;
}

}  // namespace

void TenantResolutionGuard::SetResolver(
    std::shared_ptr<Tenancy::TenantResolver> resolver,
    Tenancy::TenancyMode mode) {
    resolver_ = std::move(resolver);
    mode_ = mode;
}

void TenantResolutionGuard::before_handle(
    crow::request& req, crow::response& res, context& ctx) {
    // Inert until a resolver is installed (pre-tenancy / non-tenant WebApps).
    if (!resolver_) return;

    // Health probes are tenant-agnostic — always allow.
    const std::string& url = req.url;
    if (IsHealthPath(url)) return;

    std::string siteKey(
        req.get_header_value(std::string(Tenancy::kSiteHeaderName)));
    std::optional<Tenancy::TenantContext> resolved = resolver_->Resolve(siteKey);

    const bool suspended =
        resolved.has_value() &&
        resolved->status == std::string(DbSchema::kTenantStatusSuspended);

    if (resolved.has_value() && !suspended) {
        // Success — stash the resolved tenant and leave the response untouched.
        ctx.resolved = true;
        ctx.tenant = std::move(*resolved);
        return;
    }

    // Failure. Choose the status code + error per the mode.
    int code;
    std::string_view error;
    if (mode_ == Tenancy::TenancyMode::Control) {
        code = 421;  // Misdirected Request — reached the wrong site.
        if (suspended) {
            error = "site_suspended";
        } else if (siteKey.empty()) {
            error = "missing_site_header";
        } else {
            error = "unknown_site";
        }
    } else {
        // Fixed mode: Resolve only fails on a contradicting header — a non-empty
        // key that isn't this single tenant's. An empty header always resolves.
        code = 400;
        error = "site_mismatch";
    }

    {
        std::lock_guard<std::mutex> lock(logMutex_);
        auto now = std::chrono::steady_clock::now();
        if (lastRejectLog_.time_since_epoch().count() == 0 ||
            now - lastRejectLog_ >= kRejectLogThrottle) {
            lastRejectLog_ = now;
            LogWarning()
                << "TenantResolutionGuard rejected request: "
                << crow::method_name(req.method) << " " << url
                << " site='" << siteKey << "' error=" << error
                << " (further rejections suppressed for "
                << kRejectLogThrottle.count() << " min)\n";
        }
    }

    res.code = code;
    res.set_header("Content-Type", "application/json");
    res.write("{\"error\":\"" + std::string(error) + "\"}");
    res.end();
}

void TenantResolutionGuard::after_handle(
    crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {
    // Nothing to do after the handler.
}

}  // namespace Endpoints
