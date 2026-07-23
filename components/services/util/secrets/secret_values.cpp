#include "secret_values.h"

#include "secret_keys.h"

namespace Secrets {
namespace Values {

// NOTE: WHEN ADDING A VALUE, PLEASE UPDATE FillInSecretsStringView
//
// Phase 1.3 componentization: brand-specific default values (studio name,
// gmail sender address, Knotty-Yoga email subjects, www.knottyyoga.com website
// address) AND yoga-studio domain-key defaults (Square, subscription,
// scheduling, class check-in, provider, facility, margin) moved app-side to
// business_logic/app_secret_values.cpp. This file registers only framework,
// non-brand defaults so util/secrets stays extraction-ready.

// Mail server configuration values (deployment infra, not brand)
inline constexpr std::string_view kMailAppPasswordValue = "ctojsngxuennlbdz";
inline constexpr std::string_view kMailServerNameValue = "smtp.gmail.com";
// Use 465 for login(SSL), 587 for tls
inline constexpr std::string_view kMailServerPortValue = "465";
inline constexpr std::string_view kMailServerMethodValue = "login";

// Website links (non-brand routing bits; the brand website ADDRESS lives
// app-side)
inline constexpr std::string_view kWebsiteApiLinkPrefixValue = "api/";
// Tenancy Phase 7.1: empty by default — the SPA falls back to its bundled logo
// asset until a tenant sets a logo URL via the secrets admin.
inline constexpr std::string_view kSiteLogoUrlValue = "";
// Phase 3.3 of the security review: the activation link is the SPA
// route the verification mail points at. The SPA reads the email and
// secret from the query string, immediately POSTs to /api/verify, and
// history.replaceState's the URL bar so the secret doesn't linger in
// browser history.
inline constexpr std::string_view kWebsiteActivationLinkValue = "verify";
inline constexpr std::string_view kWebsiteLoginLinkValue = "login/";

// Auth related values
// This value is a week in microseconds
inline constexpr std::string_view kAuthVerifyEmailTimeLimitInMicrosValue = "604800000000";

// Email verifaction related values
// Phase 3.3 of the security review: expiration window dropped from one
// week to one hour. The verification mail is sent immediately after
// registration; a one-hour window covers the realistic
// "open-the-email-and-click" interval without leaving stale tokens
// alive. Operators who need a longer window can override via the
// secrets table.
// One hour in microseconds: 60 * 60 * 1,000,000 = 3,600,000,000
inline constexpr std::string_view kEmailVerificationExpirationWindowInMicrosValue = "3600000000";
inline constexpr std::string_view kEmailVerificationAttemptLimitValue = "5";

// Authentication relates settings
// This is 12 hours in microseconds 12*60*60*1,000,000 = 43200000000
inline constexpr std::string_view kAuthSessionMaxDuractioninMicrosValue = "43200000000";
// This is 30 days in microseconds 30*24*60*60*1,000,000 = 2592000000000
inline constexpr std::string_view kAuthDeviceTokenMaxDurationInMicrosValue = "2592000000000";
// This is 19 minutes in microseconds 19*60*1,000,000 = 1140000000
inline constexpr std::string_view kAuthSessionLastSeenUpdateDurationInMicrosValue = "1140000000";

// Argon2id password-hash cost — production defaults to MODERATE
// (3 ops, 256 MB). The values match libsodium's
// crypto_pwhash_OPSLIMIT_MODERATE / crypto_pwhash_MEMLIMIT_MODERATE
// constants. Phase 2.5 of the security review. The test secrets helper
// overrides these to INTERACTIVE values for speed.
inline constexpr std::string_view kAuthArgon2OpsLimitValue = "3";
inline constexpr std::string_view kAuthArgon2MemLimitKbValue = "262144";

// Phase 5 of the security review: brute-force defense thresholds.
// 15 minutes in microseconds: 15 * 60 * 1,000,000 = 900,000,000.
inline constexpr std::string_view kAuthLoginMaxFailuresPerEmailPerWindowValue = "10";
inline constexpr std::string_view kAuthLoginMaxFailuresPerIpPerWindowValue = "50";
inline constexpr std::string_view kAuthLoginFailureWindowInMicrosValue = "900000000";
// Account locks for 30 minutes after the threshold trips.
// 30 minutes in microseconds: 30 * 60 * 1,000,000 = 1,800,000,000.
inline constexpr std::string_view kAuthAccountLockoutAfterFailuresValue = "10";
inline constexpr std::string_view kAuthAccountLockoutDurationInMicrosValue = "1800000000";
// Verify is per-IP only — looser than login because legitimate
// users sometimes have flaky email links and re-try.
inline constexpr std::string_view kAuthVerifyMaxFailuresPerIpPerWindowValue = "30";
inline constexpr std::string_view kAuthVerifyFailureWindowInMicrosValue = "900000000";
// Remember-me is per-IP only — bumped vs login because legitimate
// SPAs can fire the remember endpoint a lot during a single session.
inline constexpr std::string_view kAuthRememberMaxFailuresPerIpPerWindowValue = "50";
inline constexpr std::string_view kAuthRememberFailureWindowInMicrosValue = "900000000";
// Retention default: 30 days. 30 * 24 * 60 * 60 * 1,000,000 = 2,592,000,000,000.
inline constexpr std::string_view kAuthLoginAttemptsRetentionInMicrosValue = "2592000000000";

// Phase 9.2 of the security review: digest delivery for admin_alerts.
// Recipient is empty by default — operators MUST set this in
// config_secrets to opt in. An empty recipient turns the digest job
// into a no-op (the endpoint short-circuits before constructing a
// mail message) so a freshly-deployed system doesn't fail-loop
// trying to send to an empty address. (The brand digest SUBJECT lives
// app-side.)
inline constexpr std::string_view kAdminAlertsRecipientValue = "";
// Initialized to 0 so the first run picks up the entire history.
// Operators who want to skip the backfill can edit the secret to
// `now_us()` once before enabling the job.
inline constexpr std::string_view kAdminAlertsLastNotifiedUsValue = "0";

// Server configuration settings
inline constexpr std::string_view kServerProductionModeValue = "false";

// Admin FK picker configuration
inline constexpr std::string_view kFkPickerPreloadThresholdValue = "50";

// Image configuration
inline constexpr std::string_view kImageMaxSourceWidthValue = "4096";
inline constexpr std::string_view kImageMaxSourceHeightValue = "4096";
inline constexpr std::string_view kImageMaxUploadBytesValue = "10485760";  // 10MB
inline constexpr std::string_view kImageMaxPublicScaledWidthValue = "800";
inline constexpr std::string_view kImageMaxPublicScaledHeightValue = "800";
// 24 hours in microseconds: 24*60*60*1,000,000 = 86400000000
inline constexpr std::string_view kScaledPhotoReaperIntervalUsValue = "86400000000";
// 30 days in microseconds: 30*24*60*60*1,000,000 = 2592000000000
inline constexpr std::string_view kScaledPhotoMaxAgeUsValue = "2592000000000";

// PLEASE KEEP THESE IN SYNC WITH secrets/secret_keys.h
void FillInSecretsStringView(std::function<void(std::string_view, std::string_view)> addSecret) {
    addSecret(kMailAppPassword, kMailAppPasswordValue);
    addSecret(kMailServerName, kMailServerNameValue);
    addSecret(kMailServerPort, kMailServerPortValue);
    addSecret(kMailServerMethod, kMailServerMethodValue);
    addSecret(kWebsiteLoginLink, kWebsiteLoginLinkValue);
    addSecret(kWebsiteApiLinkPrefix, kWebsiteApiLinkPrefixValue);
    addSecret(kWebsiteActivationLink, kWebsiteActivationLinkValue);
    addSecret(kSiteLogoUrl, kSiteLogoUrlValue);
    addSecret(kAuthVerifyEmailTimeLimitInMicros, kAuthVerifyEmailTimeLimitInMicrosValue);
    addSecret(kEmailVerificationExpirationWindowInMicros, kEmailVerificationExpirationWindowInMicrosValue);
    addSecret(kEmailVerificationAttemptLimit, kEmailVerificationAttemptLimitValue);
    addSecret(kAuthSessionMaxDuractioninMicros, kAuthSessionMaxDuractioninMicrosValue);
    addSecret(kAuthDeviceTokenMaxDurationInMicros, kAuthDeviceTokenMaxDurationInMicrosValue);
    addSecret(kAuthSessionLastSeenUpdateDurationInMicros, kAuthSessionLastSeenUpdateDurationInMicrosValue);
    addSecret(kAuthArgon2OpsLimit, kAuthArgon2OpsLimitValue);
    addSecret(kAuthArgon2MemLimitKb, kAuthArgon2MemLimitKbValue);
    addSecret(kAuthLoginMaxFailuresPerEmailPerWindow, kAuthLoginMaxFailuresPerEmailPerWindowValue);
    addSecret(kAuthLoginMaxFailuresPerIpPerWindow, kAuthLoginMaxFailuresPerIpPerWindowValue);
    addSecret(kAuthLoginFailureWindowInMicros, kAuthLoginFailureWindowInMicrosValue);
    addSecret(kAuthAccountLockoutAfterFailures, kAuthAccountLockoutAfterFailuresValue);
    addSecret(kAuthAccountLockoutDurationInMicros, kAuthAccountLockoutDurationInMicrosValue);
    addSecret(kAuthVerifyMaxFailuresPerIpPerWindow, kAuthVerifyMaxFailuresPerIpPerWindowValue);
    addSecret(kAuthVerifyFailureWindowInMicros, kAuthVerifyFailureWindowInMicrosValue);
    addSecret(kAuthRememberMaxFailuresPerIpPerWindow, kAuthRememberMaxFailuresPerIpPerWindowValue);
    addSecret(kAuthRememberFailureWindowInMicros, kAuthRememberFailureWindowInMicrosValue);
    addSecret(kAuthLoginAttemptsRetentionInMicros, kAuthLoginAttemptsRetentionInMicrosValue);
    addSecret(kAdminAlertsRecipient, kAdminAlertsRecipientValue);
    addSecret(kAdminAlertsLastNotifiedUs, kAdminAlertsLastNotifiedUsValue);
    addSecret(kServerProductionMode, kServerProductionModeValue);
    addSecret(kFkPickerPreloadThreshold, kFkPickerPreloadThresholdValue);
    addSecret(kImageMaxSourceWidth, kImageMaxSourceWidthValue);
    addSecret(kImageMaxSourceHeight, kImageMaxSourceHeightValue);
    addSecret(kImageMaxUploadBytes, kImageMaxUploadBytesValue);
    addSecret(kImageMaxPublicScaledWidth, kImageMaxPublicScaledWidthValue);
    addSecret(kImageMaxPublicScaledHeight, kImageMaxPublicScaledHeightValue);
    addSecret(kScaledPhotoReaperIntervalUs, kScaledPhotoReaperIntervalUsValue);
    addSecret(kScaledPhotoMaxAgeUs, kScaledPhotoMaxAgeUsValue);
}

void FillInSecretsString(std::function<void(const std::string&, const std::string&)> addSecret) {
    FillInSecretsStringView(
        [&](std::string_view key, std::string_view value) {
            addSecret(std::string(key), std::string(value));
        });
}

}
}  // namespace Secrets
