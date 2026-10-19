#include "password_reset_helper.h"

#include <stdexcept>
#include <string>

#include "business_logic/auth/auth_helper.h"
// person.h before password_reset_mail.h: person.h names `Mail::MailHelperPtr`
// from inside namespace Auth, which stops resolving to ::Mail once the mail
// builders' Auth::Mail namespace has been declared.
#include "business_logic/auth/person.h"
#include "business_logic/auth/password_reset_mail.h"
#include "db_schema/password_reset_tokens.h"
#include "db_schema/people.h"
#include "util/logging.h"
#include "util/mail/tenant_branding.h"
#include "util/secrets/secret_keys.h"
#include "util/thread_pool.h"

namespace Auth {

namespace {

// 32 random bytes: 43 characters of URL-safe base64 in the link.
constexpr size_t kSecretBytes = 32;
constexpr int64_t kMinuteUs = 60LL * 1000000LL;
// Used when the secrets row is missing or unusable — an upgraded database has
// no row until someone adds one.
constexpr int64_t kDefaultTtlMinutes = 60;
constexpr std::string_view kDefaultResetPath = "reset-password";

std::string ReadColumn(const KeyValueTable& row, std::string_view column) {
    auto it = row.find(std::string(column));
    return (it == row.end()) ? std::string() : it->second;
}

int64_t LookupTtlMinutes(
    Transaction& transaction, const Secrets::SecretsHelperPtr& secrets) {
    try {
        int64_t minutes = std::stoll(secrets->LookupSecret(
            transaction, Secrets::kAuthPasswordResetTtlMinutes));
        if (minutes > 0) {
            return minutes;
        }
    } catch (...) {
    }
    return kDefaultTtlMinutes;
}

// The hash the presented secret would have been stored under, or "" when it
// isn't valid base64. A stored hash is never empty, so "" can't match.
std::string HashPresentedSecret(std::string_view secret) {
    try {
        AuthHelper auth;
        return auth.HashBinary(auth.Base64Decode(std::string(secret)));
    } catch (...) {
        return {};
    }
}

}  // namespace

PasswordResetHelper::PasswordResetHelper(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper),
      people_(databaseHelper),
      tokens_(databaseHelper) {
}

int64_t PasswordResetHelper::RequestReset(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    ::Mail::MailHelperPtr mail,
    std::string_view email) {
    if (!secrets) {
        throw std::invalid_argument(
            "PasswordResetHelper::RequestReset - secrets is null");
    }
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        return 0;
    }
    const int64_t personId = std::stoll(ReadColumn(person, DbSchema::kPeopleId));

    AuthHelper auth;
    const Blob secretBytes = auth.RandomBytes(kSecretBytes);
    const int64_t ttlMinutes = LookupTtlMinutes(transaction, secrets);
    const int64_t nowUs = std::stoll(
        transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
    tokens_.ReplaceForPerson(
        transaction, personId, auth.HashBinary(secretBytes),
        nowUs + ttlMinutes * kMinuteUs);

    Mail::PasswordResetMailData data;
    data.firstName = ReadColumn(person, DbSchema::kPeopleFirstName);
    // The stored address, not the typed one: email is CITEXT, so they can
    // differ in case.
    data.email = ReadColumn(person, DbSchema::kPeopleEmail);
    data.secret = auth.Base64Encode(secretBytes);
    data.resetPath = secrets->LookupSecret(
        transaction, Secrets::kWebsitePasswordResetLink);
    if (data.resetPath.empty()) {
        data.resetPath = std::string(kDefaultResetPath);
    }
    data.ttlMinutes = ttlMinutes;

    const ::Mail::TenantBranding branding =
        ::Mail::LoadTenantBranding(*secrets, transaction);
    ::Mail::MailMessage message(
        ::Mail::LoadSenderAddress(*secrets, transaction),
        ::Mail::MailAddress{
            data.firstName + " " + ReadColumn(person, DbSchema::kPeopleLastName),
            data.email });
    message.SetSubject(Mail::GeneratePasswordResetMailSubject(branding));
    message.SetBodyHtml(Mail::GeneratePasswordResetMailBody(data, branding));

    if (mail) {
        // Sending is the slow part (an SMTP round trip). Queued, it no longer
        // shows in the response time, which would otherwise say whether the
        // address has an account.
        ThreadPool::GetInstance().Queue([mail, message]() {
            try {
                mail->SendMail(message);
            } catch (const std::exception& e) {
                LogError() << "[password_reset] event=mail_failed error=\""
                           << e.what() << "\"\n";
            } catch (...) {
                LogError() << "[password_reset] event=mail_failed error=unknown\n";
            }
        });
    }
    return personId;
}

PasswordResetResult PasswordResetHelper::CompleteReset(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view email,
    std::string_view secret,
    std::string_view newPassword) {
    PasswordResetResult result;
    // The rules registration enforces: a password is required.
    if (newPassword.empty()) {
        result.outcome = PasswordResetOutcome::PasswordRejected;
        return result;
    }

    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        return result;
    }
    const int64_t personId = std::stoll(ReadColumn(person, DbSchema::kPeopleId));

    // Claim an attempt BEFORE comparing, so every presented secret counts,
    // right or wrong; an expired link, or one out of tries, claims nothing.
    const std::string presentedHash = HashPresentedSecret(secret);
    KeyValueTable link = tokens_.IncrementAttempts(
        transaction, personId, kAttemptLimit);
    if (link.empty()) {
        return result;
    }
    if (!AuthHelper::ConstantTimeEqual(
            ReadColumn(link, DbSchema::kPasswordResetTokensTokenHash),
            presentedHash)) {
        if (std::stoll(ReadColumn(link, DbSchema::kPasswordResetTokensAttempts))
                >= kAttemptLimit) {
            tokens_.DeleteForPerson(transaction, personId);
        }
        return result;
    }

    // Following the link proves the inbox. Verifying here also rescues an
    // account whose activation link expired before it was clicked, and has to
    // come first: PersonHelper only changes a verified account's password.
    const std::string storedEmail = ReadColumn(person, DbSchema::kPeopleEmail);
    if (ReadColumn(person, DbSchema::kPeopleEmailVerifiedAt).empty()) {
        people_.VerifyPersonEmail(transaction, storedEmail);
    }
    PersonHelper personHelper(databaseHelper_);
    // With `secrets`, the hash takes the production Argon2id cost.
    if (!personHelper.UpdatePassword(
            transaction, storedEmail, newPassword, secrets)) {
        throw std::runtime_error(
            "PasswordResetHelper::CompleteReset - password update failed");
    }
    people_.SetMustChangePassword(transaction, personId, false);
    people_.ClearLoginLockout(transaction, personId);
    tokens_.DeleteForPerson(transaction, personId);
    // Whoever knew the old password is signed out everywhere: every session
    // and every remember-me device token goes.
    personHelper.LogoutPerson(transaction, personId);

    result.outcome = PasswordResetOutcome::Success;
    result.personId = personId;
    return result;
}

}  // namespace Auth
