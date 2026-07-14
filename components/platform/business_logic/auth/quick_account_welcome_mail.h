#pragma once

#include <string>

#include "util/mail/tenant_branding.h"

namespace Auth {
namespace Mail {

struct QuickAccountWelcomeData {
    std::string firstName;
    std::string email;
    std::string temporaryPassword;
};

// Generate HTML body for the welcome email sent to quick-created accounts.
// Phase 1.3 componentization: the studio name is substituted from the
// app-supplied TenantBranding rather than hardcoded, so this framework mail
// body ships to the public honuware repo brand-free.
std::string GenerateQuickAccountWelcomeBody(
    const QuickAccountWelcomeData& data,
    const ::Mail::TenantBranding& branding);

}  // namespace Mail
}  // namespace Auth
