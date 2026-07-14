#include "cloudfront_origin_guard.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include <crow/common.h>

#include "util/env.h"
#include "util/logging.h"

namespace Endpoints {
namespace {

// Paths the guard always lets through. The full URL prefix is matched, so
// `/api/health/db` (a hypothetical sub-resource) would also pass.
constexpr std::string_view kHealthPathPrefix = "/api/health";

constexpr std::string_view kOriginSecretHeader = "X-Origin-Secret";

constexpr std::chrono::minutes kRejectLogThrottle{1};

constexpr std::string_view kForbiddenBody =
    R"({"error":"direct_origin_access_forbidden"})";

}  // namespace

CloudFrontOriginGuard::CloudFrontOriginGuard() {
    // Canonical HONUWARE_* name first, legacy KNOTTYYOGA_* fallback (Phase 1.1).
    const char* val = Util::GetEnvWithFallback(
        "HONUWARE_ORIGIN_SECRET", "KNOTTYYOGA_ORIGIN_SECRET");
    if (val != nullptr && val[0] != '\0') {
        expectedSecret_.assign(val);
        active_ = true;
        LogInfo() << "CloudFrontOriginGuard active: requests must carry "
                  << kOriginSecretHeader << " (allow-listed: "
                  << kHealthPathPrefix << "*)\n";
    } else {
        LogInfo() << "CloudFrontOriginGuard disabled: HONUWARE_ORIGIN_SECRET"
                     " not set. All requests pass through. Set the env var"
                     " in production to require the CloudFront origin secret.\n";
    }
}

void CloudFrontOriginGuard::before_handle(
    crow::request& req,
    crow::response& res,
    context& /*ctx*/) {
    // Disabled: pass-through for local dev where env var isn't set.
    if (!active_) return;

    // Allow-list health probes regardless of the header.
    const std::string& url = req.url;
    if (url.size() >= kHealthPathPrefix.size() &&
        url.compare(0, kHealthPathPrefix.size(), kHealthPathPrefix) == 0) {
        return;
    }

    std::string presented(req.get_header_value(std::string(kOriginSecretHeader)));
    if (!presented.empty() && presented == expectedSecret_) return;

    // Reject: 403 with a small JSON body.
    {
        std::lock_guard<std::mutex> lock(logMutex_);
        auto now = std::chrono::steady_clock::now();
        if (lastRejectLog_.time_since_epoch().count() == 0 ||
            now - lastRejectLog_ >= kRejectLogThrottle) {
            lastRejectLog_ = now;
            LogWarning()
                << "CloudFrontOriginGuard rejected direct-origin request: "
                << crow::method_name(req.method) << " " << url
                << " (header "
                << (presented.empty() ? "missing" : "mismatched")
                << "; further rejections suppressed for "
                << kRejectLogThrottle.count() << " min)\n";
        }
    }

    res.code = 403;
    res.set_header("Content-Type", "application/json");
    res.write(std::string(kForbiddenBody));
    res.end();
}

void CloudFrontOriginGuard::after_handle(
    crow::request& /*req*/,
    crow::response& /*res*/,
    context& /*ctx*/) {
    // Nothing to do after the handler.
}

}  // namespace Endpoints
