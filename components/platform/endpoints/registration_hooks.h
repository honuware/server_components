#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

class Transaction;
class EndpointAuthHelper;

namespace Endpoints {

// Post-registration extension point (H8 extraction seam).
//
// A consuming app MAY register one of these on the WebApp service registry —
// webApp.SetService<Endpoints::PostRegisterHook>(hook) — to run app-specific work
// after a successful /api/register, inside the registration transaction. The
// framework calls it best-effort (swallowing exceptions) so app follow-up can
// never block or fail a registration. When no hook is registered, registration
// proceeds with no extra work — the correct default for a consumer (e.g.
// CommunityFinder) that has no post-register step.
//
// The hook receives the request's EndpointAuthHelper (for tenant-correct
// db/secrets/mail) plus the new person's id + email. knottyyoga registers one that
// converts pending gift-permission invitations for the new user.
struct PostRegisterHook {
    std::function<void(
        EndpointAuthHelper& auth,
        Transaction& transaction,
        std::int64_t personId,
        std::string_view email)> onRegistered;
};

}  // namespace Endpoints
