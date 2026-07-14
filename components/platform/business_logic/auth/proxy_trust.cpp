#include "proxy_trust.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include "util/env.h"
#include "util/types.h"

namespace Auth {

bool ProxyTrustEnabled() {
    // Canonical HONUWARE_* name first, legacy KNOTTYYOGA_* as fallback (Phase 1.1).
    const char* val = Util::GetEnvWithFallback(
        "HONUWARE_TRUST_PROXY", "KNOTTYYOGA_TRUST_PROXY");
    if (val == nullptr || val[0] == '\0') return false;
    std::string lower = StringToLower(val);
    return lower == "1" || lower == "true";
}

std::string ResolveViewerScheme(const crow::request& req) {
    if (!ProxyTrustEnabled()) return "";
    return StringTrim(req.get_header_value("X-Forwarded-Proto"));
}

std::string ResolveViewerIp(const crow::request& req) {
    if (!ProxyTrustEnabled()) return "";
    std::string forwarded(req.get_header_value("X-Forwarded-For"));
    if (forwarded.empty()) return "";

    // X-Forwarded-For is "client, proxy1, proxy2, ..."; only the first
    // entry is the actual viewer. Everything after is the chain of
    // intermediate hops and would be wrong to attribute to the viewer.
    std::string_view view(forwarded);
    auto comma = view.find(',');
    std::string_view first = (comma == std::string_view::npos)
        ? view
        : view.substr(0, comma);
    return StringTrim(first);
}

std::string ResolveClientIp(const crow::request& req) {
    // Trusted proxy: ResolveViewerIp returns the first X-Forwarded-For
    // entry (the actual viewer). When unset, fall through to Crow's
    // connection peer.
    std::string viewer = ResolveViewerIp(req);
    if (!viewer.empty()) return viewer;

    // No trusted proxy → use the connection peer address. When tests
    // synthesize a crow::request without a real socket, this is empty;
    // the gate / recorder treat empty as "skip the per-IP path".
    return req.remote_ip_address;
}

}  // namespace Auth
