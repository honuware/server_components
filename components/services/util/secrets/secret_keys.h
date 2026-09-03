#pragma once

#include <string_view>

namespace Secrets {

// NOTE: WHEN ADDING A VALUE, PLEASE UPDATE FillInSecretsStringView in secret_values.cpp

// Mail server configuration keys
inline constexpr std::string_view kMailAppPassword = "mail_app_password";
inline constexpr std::string_view kMailServerName = "mail_server_name";
inline constexpr std::string_view kMailServerPort = "mail_server_port";
inline constexpr std::string_view kMailServerMethod = "mail_server_method";
// Phase 3.2: these are secret KEY NAMES (like every other key here); the
// brand-specific default VALUES ("Knotty Yoga and Spa" / the gmail address) are
// registered app-side in business_logic/app_secret_values.cpp. Consumers MUST
// look them up via SecretsHelper (see Mail::LoadSenderAddress) — never use the
// constant as the value.
inline constexpr std::string_view kMailSenderName = "mail_sender_name";
inline constexpr std::string_view kMailSenderAddress = "mail_sender_address";
inline constexpr std::string_view kMailActivationEmailSubject = "activation_email_subject";

// Website links
inline constexpr std::string_view kWebsiteAddress = "website_address";
inline constexpr std::string_view kWebsiteAddressLogin = "website_address_login";
inline constexpr std::string_view kWebsiteApiLinkPrefix = "api_link_prefix";
inline constexpr std::string_view kWebsiteActivationLink = "activation_link";
inline constexpr std::string_view kWebsiteLoginLink = "login_link";

// Tenancy plan Phase 7.1: per-tenant public branding. The SPA reads these via
// GET /api/site_info to brand a shared bundle at runtime. Brand-free KEY name;
// default VALUE is empty (the SPA falls back to its bundled logo asset), and each
// tenant sets its own via the secrets admin. Not per-brand, so it lives here
// (framework) rather than app_secret_keys.h.
inline constexpr std::string_view kSiteLogoUrl = "site_logo_url";

// Tenant Theming and Branding Phase 1: the public CONTENT SLOTS a tenant fills
// in so one built SPA bundle renders that studio's own copy. Every key here is
// served by GET /api/site_info's `content` object (see
// business_logic/branding/site_content_slots.h, which is the registry that
// decides which keys are public and how each is validated).
//
// KEY names are brand-free framework surface; the brand-content default VALUES
// are registered app-side (business_logic/app_secret_values.cpp), exactly as
// kMailSenderName does — config_secrets.name is UNIQUE, so a key gets its
// default from the framework OR the app, never both. The two URL slots below
// are the exception: their neutral default is genuinely framework-owned (empty
// == "use the SPA's bundled asset", the contract kSiteLogoUrl established), so
// they are defaulted in secret_values.cpp.
inline constexpr std::string_view kSiteLogoAlt = "site_logo_alt";
inline constexpr std::string_view kSiteBrowserTitle = "site_browser_title";
inline constexpr std::string_view kSiteFaviconUrl = "site_favicon_url";
inline constexpr std::string_view kSiteHeroHeadline = "site_hero_headline";
inline constexpr std::string_view kSiteHeroSubline = "site_hero_subline";
inline constexpr std::string_view kSiteHeroImageUrl = "site_hero_image_url";
inline constexpr std::string_view kSiteTaglineLines = "site_tagline_lines";
// The footer tagline as ARTWORK. When set, the footer renders this image and
// `site_tagline_lines` becomes its alt text; when empty, the lines are drawn as
// styled text. Two slots rather than one because the words are still needed for
// screen readers and for a tenant with no artwork.
inline constexpr std::string_view kSiteTaglineImageUrl = "site_tagline_image_url";
inline constexpr std::string_view kSiteAddressLines = "site_address_lines";
inline constexpr std::string_view kSiteContactEmail = "site_contact_email";
// The heading above the About page's body. Empty means "keep the SPA's own
// sentence", which interpolates the tenant name — so a site that never touches
// this reads exactly as it did before the slot existed. Framework-defaulted to
// "" for that reason, like the favicon and hero image.
inline constexpr std::string_view kSiteAboutHeading = "site_about_heading";
inline constexpr std::string_view kSiteAboutMarkdown = "site_about_markdown";
inline constexpr std::string_view kSiteStartIntro = "site_start_intro";
inline constexpr std::string_view kSiteMembershipBlurb = "site_membership_blurb";
// Freeform "label|url" lines, one social destination per line (OQ-T4).
inline constexpr std::string_view kSiteSocialLinks = "site_social_links";

// Tenant Theming Phase 4: the DESIGN TOKENS (`site_theme_*`) are deliberately
// NOT declared one-constant-per-key here. There are ~60 of them, nothing outside
// the theming code ever names one individually, and the mapping that matters is
// key -> CSS custom property — which only a table can express. The registry in
// business_logic/branding/site_theme_tokens.cpp is their single source of truth.
// They also have no default VALUES on either side: the SPA's stylesheet holds
// the defaults, so "no config_secrets row" is the normal state for a token.

// Auth related keys
inline constexpr std::string_view kAuthVerifyEmailTimeLimitInMicros = "auth_verify_email_time_limit_in_micros";

// Email verifaction related keys
inline constexpr std::string_view kEmailVerificationExpirationWindowInMicros = "email_verification_expiration_window_in_micros";
inline constexpr std::string_view kEmailVerificationAttemptLimit = "email_verification_attempt_limit";

// Authentication relates settings
inline constexpr std::string_view kAuthSessionMaxDuractioninMicros = "auth_session_max_duration_in_micros";
inline constexpr std::string_view kAuthDeviceTokenMaxDurationInMicros = "auth_device_token_max_duration_in_micros";
inline constexpr std::string_view kAuthSessionLastSeenUpdateDurationInMicros = "auth_session_last_seen_update_duration_in_micros";

// Argon2id password-hash cost parameters (Phase 2.5 of the security
// review). Production defaults are MODERATE — 3 ops, 256 MB. Tests
// override these to INTERACTIVE in SecretsHelperTestImpl so the suite
// stays fast.
inline constexpr std::string_view kAuthArgon2OpsLimit = "auth_argon2_ops_limit";
inline constexpr std::string_view kAuthArgon2MemLimitKb = "auth_argon2_mem_limit_kb";

// Phase 5 of the security review: rate-limit / brute-force defense.
// All counts are "failures within the window"; successes don't count.
// Login bucket — per-email and per-IP independently. Hitting either
// threshold returns the same generic 429 to the client.
inline constexpr std::string_view kAuthLoginMaxFailuresPerEmailPerWindow = "auth_login_max_failures_per_email";
inline constexpr std::string_view kAuthLoginMaxFailuresPerIpPerWindow = "auth_login_max_failures_per_ip";
inline constexpr std::string_view kAuthLoginFailureWindowInMicros = "auth_login_failure_window_in_micros";
// Account lockout — sticky on `people.locked_until` for the duration.
// Triggered by a single UPDATE in the same transaction as a failed
// password-verify, so two parallel attempts at the threshold can
// over-count by at most one (acceptable).
inline constexpr std::string_view kAuthAccountLockoutAfterFailures = "auth_account_lockout_after_failures";
inline constexpr std::string_view kAuthAccountLockoutDurationInMicros = "auth_account_lockout_duration_in_micros";
// Verify (email-confirmation) bucket — per-IP only; we don't have an
// authenticated email here.
inline constexpr std::string_view kAuthVerifyMaxFailuresPerIpPerWindow = "auth_verify_max_failures_per_ip";
inline constexpr std::string_view kAuthVerifyFailureWindowInMicros = "auth_verify_failure_window_in_micros";
// Remember-me (device-token reauth) bucket — per-IP only.
inline constexpr std::string_view kAuthRememberMaxFailuresPerIpPerWindow = "auth_remember_max_failures_per_ip";
inline constexpr std::string_view kAuthRememberFailureWindowInMicros = "auth_remember_failure_window_in_micros";
// Retention for the login_attempts table — purged by the periodic
// cleanup job.
inline constexpr std::string_view kAuthLoginAttemptsRetentionInMicros = "auth_login_attempts_retention_in_micros";

// Phase 9.2 of the security review: digest delivery for admin_alerts.
// The scheduler hits /api/admin/notify_admin_alerts every N minutes;
// the endpoint reads admin_alerts inserted since `last_notified_us`,
// emails them to `recipient`, and stamps last_notified_us forward so
// the same alert is never mailed twice. Empty `recipient` makes the
// endpoint a no-op (production hasn't configured a recipient yet).
inline constexpr std::string_view kAdminAlertsRecipient = "admin_alerts_recipient";
inline constexpr std::string_view kAdminAlertsLastNotifiedUs = "admin_alerts_last_notified_us";
inline constexpr std::string_view kAdminAlertsDigestSubject = "admin_alerts_digest_subject";

// Server configuration settings
inline constexpr std::string_view kServerProductionMode = "production_mode_on";

// Payment confirmation email
inline constexpr std::string_view kMailPaymentConfirmationSubject = "payment_confirmation_email_subject";

// Admin FK picker configuration
inline constexpr std::string_view kFkPickerPreloadThreshold = "fk_picker_preload_threshold";

// Image configuration
inline constexpr std::string_view kImageMaxSourceWidth = "image_max_source_width";
inline constexpr std::string_view kImageMaxSourceHeight = "image_max_source_height";
inline constexpr std::string_view kImageMaxUploadBytes = "image_max_upload_bytes";
inline constexpr std::string_view kImageMaxPublicScaledWidth = "image_max_public_scaled_width";
inline constexpr std::string_view kImageMaxPublicScaledHeight = "image_max_public_scaled_height";
inline constexpr std::string_view kScaledPhotoReaperIntervalUs = "scaled_photo_reaper_interval_us";
inline constexpr std::string_view kScaledPhotoMaxAgeUs = "scaled_photo_max_age_us";

// NOTE: yoga-studio DOMAIN keys (Square, subscription, scheduling, class
// check-in, provider/time-off/shift, facility buffers, non-member margin) moved
// to business_logic/app_secret_keys.h (Phase 1.3) so this framework header stays
// domain-free for the honuware extraction. They remain in namespace Secrets, so
// callers include that header alongside this one.

}  // namespace Secrets