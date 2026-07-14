#include "quick_account_welcome_mail.h"

#include "util/types.h"

namespace Auth {
namespace Mail {

namespace {

constexpr std::string_view kWelcomeMailTemplate =
R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Welcome to {studio_name}</title>
    <style>
        body { font-family: Arial, sans-serif; line-height: 1.6; color: #333; max-width: 600px; margin: 0 auto; padding: 20px; }
        .header { background-color: #4a5568; color: white; padding: 20px; text-align: center; }
        .content { padding: 20px; background-color: #f7fafc; }
        .details { background-color: #edf2f7; padding: 15px; margin: 15px 0; border-radius: 4px; }
        .password { font-family: monospace; font-size: 18px; font-weight: bold; color: #2d3748; }
        .warning { color: #c53030; font-weight: bold; }
        .footer { text-align: center; padding: 20px; color: #718096; font-size: 12px; }
    </style>
</head>
<body>
    <div class="header">
        <h1>{studio_name}</h1>
        <h2>Welcome!</h2>
    </div>
    <div class="content">
        <p>Hello {first_name},</p>
        <p>An account has been created for you at {studio_name}. You can use it to view your bookings, manage your profile, and more.</p>
        <div class="details">
            <p><strong>Email:</strong> {email}</p>
            <p><strong>Temporary Password:</strong> <span class="password">{temporary_password}</span></p>
        </div>
        <p class="warning">Please change your password immediately after your first login.</p>
        <p>If you have any questions, please don't hesitate to contact us.</p>
    </div>
    <div class="footer">
        <p>{studio_name}</p>
    </div>
</body>
</html>
)HTML";

}  // namespace

std::string GenerateQuickAccountWelcomeBody(
    const QuickAccountWelcomeData& data,
    const ::Mail::TenantBranding& branding) {
    KeyValueTable params;
    params["first_name"] = data.firstName;
    params["email"] = data.email;
    params["temporary_password"] = data.temporaryPassword;
    params["studio_name"] = branding.studioName;

    return NormalizeCrLf(FormatString(kWelcomeMailTemplate, params));
}

}  // namespace Mail
}  // namespace Auth
