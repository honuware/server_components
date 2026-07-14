#include "quick_account_helper.h"

#include <utility>

#include "business_logic/auth/auth_helper.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/quick_account_welcome_mail.h"
#include "db_schema/people.h"
#include "util/mail/tenant_branding.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"

namespace Auth {

namespace {

// Random alphanumeric temporary password. Ambiguous glyphs (I/l/1, O/0)
// are excluded — the person types this from an email.
std::string GenerateRandomPassword(int length = 12) {
    static constexpr std::string_view kChars =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#";
    AuthHelper auth;
    Blob randomBytes = auth.RandomBytes(length);
    std::string password;
    password.reserve(length);
    for (int i = 0; i < length; ++i) {
        password.push_back(
            kChars[static_cast<uint8_t>(randomBytes[i]) % kChars.size()]);
    }
    return password;
}

std::string ReadColumn(const KeyValueTable& row, std::string_view column) {
    auto it = row.find(std::string(column));
    return (it == row.end()) ? std::string() : it->second;
}

}  // namespace

QuickAccountHelper::QuickAccountHelper(
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelperPtr secretsHelper,
    ::Mail::MailHelperPtr mailHelper,
    QuickAccountPostCreateHook postCreateHook)
    : databaseHelper_(databaseHelper),
      secretsHelper_(std::move(secretsHelper)),
      mailHelper_(std::move(mailHelper)),
      postCreateHook_(std::move(postCreateHook)),
      people_(databaseHelper) {
}

QuickAccountResult QuickAccountHelper::EnsureAccountWithWelcome(
    Transaction& transaction,
    const std::string& firstName,
    const std::string& lastName,
    const std::string& email) {
    QuickAccountResult result;

    KeyValueTable existing = people_.LookupPersonByEmail(transaction, email);
    if (!existing.empty()) {
        result.personId = std::stoll(ReadColumn(existing, DbSchema::kPeopleId));
        result.alreadyExists = true;
        result.firstName = ReadColumn(existing, DbSchema::kPeopleFirstName);
        result.lastName = ReadColumn(existing, DbSchema::kPeopleLastName);
        return result;
    }

    std::string temporaryPassword = GenerateRandomPassword();

    // Fully validated — the staffer vouches for the email in person, so no
    // verification round-trip. must_change_password forces a new password on
    // the first login with the emailed temporary one.
    PersonHelper personHelper(databaseHelper_);
    PersonInfo info{ email, firstName, lastName };
    personHelper.CreateFullyValidatedUser(
        transaction, info, temporaryPassword, secretsHelper_);

    KeyValueTable created = people_.LookupPersonByEmail(transaction, email);
    result.personId = std::stoll(ReadColumn(created, DbSchema::kPeopleId));
    result.firstName = firstName;
    result.lastName = lastName;

    people_.SetMustChangePassword(transaction, result.personId, true);

    // App-specific post-creation side effects (e.g. converting pending
    // gift-permission invitations addressed to this email). Kept out of auth
    // itself via the hook so this framework helper doesn't depend on app
    // modules. Failures must not block the account creation itself.
    if (postCreateHook_) {
        try {
            postCreateHook_(transaction, result.personId, email);
        } catch (const std::exception&) {
        }
    }

    if (mailHelper_ && secretsHelper_) {
        Mail::QuickAccountWelcomeData emailData;
        emailData.firstName = firstName;
        emailData.email = email;
        emailData.temporaryPassword = temporaryPassword;

        // Phase 1.3 componentization: brand identity for the (framework)
        // welcome-mail builder comes from app-supplied values rather than being
        // hardcoded in the template. The studio name doubles as the From-header
        // display name today; TenantBranding keeps the fields separate so the
        // tenancy project can diverge them per-tenant.
        ::Mail::TenantBranding branding =
            ::Mail::LoadTenantBranding(*secretsHelper_, transaction);

        std::string htmlBody =
            Mail::GenerateQuickAccountWelcomeBody(emailData, branding);
        ::Mail::MailAddress from{branding.senderName, branding.senderAddress};
        ::Mail::MailAddress to{firstName, email};
        ::Mail::MailMessage message(from, to);
        message.SetSubject(
            "Welcome to " + branding.studioName + " - Your Account");
        message.SetBodyHtml(htmlBody);
        try { mailHelper_->SendMail(message); } catch (...) {}
    }

    return result;
}

}  // namespace Auth
