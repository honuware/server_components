#include "person.h"

#include <sodium.h>
#include <stdexcept>
#include <string>

#include "db_schema/device_tokens.h"
#include "db_schema/people.h"
#include "db_schema/sessions.h"
#include "business_logic/auth/auth_helper.h"
#include "util/secrets/secrets_helper.h"
#include "util/secrets/secret_keys.h"
#include "sql_util/database_common.h" // StringFromInt etc.
#include "util/mail/mail_helper.h"
#include "sql_util/table_helpers/email_verifications.h"
#include "person_verify_mail.h"
#include "sql_util/table_helpers/device_tokens.h"
#include "sql_util/table_helpers/sessions.h"
#include "db_schema/roles.h"
#include "db_schema/permissions.h"
#include "db_schema/role_permissions.h"
#include "db_schema/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "util/types.h"
#include "util/thread_pool.h"

namespace Auth {

namespace {

constexpr int64_t kYearInMicros = 365LL * 24LL * 60LL * 60LL * 1000000LL;

// Read the verification window from secrets; fall back to one year on failure.
int64_t GetVerifyWindowMicros(Transaction& transaction, const Secrets::SecretsHelperPtr& secrets) {
    if (!secrets) return kYearInMicros;
    try {
        std::string val = secrets->LookupSecret(
            transaction, Secrets::kAuthVerifyEmailTimeLimitInMicros);
        if (val.empty()) return kYearInMicros;
        return std::stoll(val);
    }
    catch (...) {
        return kYearInMicros;
    }
}

bool IsVerified(const KeyValueTable& keyValueTable) {
    if (keyValueTable.empty()) {
        return false;
    }
    return !keyValueTable.at(std::string(DbSchema::kPeopleEmailVerifiedAt)).empty();
}

bool DeviceTokenHelper(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string& outSecretHash,
    std::string& base64EncodedSecret,
    int64_t& outExpiresAtMicros) {
    AuthHelper auth;
    Blob secretBytes = auth.RandomBytes(64);
    outSecretHash = auth.HashBinary(secretBytes);
    base64EncodedSecret = auth.Base64Encode(secretBytes);
    int64_t maxDurationMicros = kYearInMicros;

    if (secrets) {
        std::string val = secrets->LookupSecret(
            transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros);
        if (!val.empty()) {
            maxDurationMicros = std::stoll(val);

            int64_t nowUs = std::stoll(
                transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));

            outExpiresAtMicros = nowUs + maxDurationMicros;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }

    return true;
}

}  // namespace

PersonHelper::PersonHelper(
    DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper), people_(databaseHelper) {
}

// Email validation workflow
void PersonHelper::PreliminaryRegisterPerson(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    ::Mail::MailHelperPtr mailHelper,
    const PersonInfo& personInfo,
    std::string_view password,
    bool sendEmail /*= true*/) {
    // Hash the password and create a person with unverified email (verified_at remains null).
    AuthHelper auth;
    std::string hash = AuthHelper::HashPasswordWithSecrets(
        transaction, secrets, std::string(password));
    int64_t id = people_.AddPerson(
        transaction, personInfo.email, personInfo.firstName, personInfo.lastName, hash);
    // Add entry to email verification table
    Blob tokenBytes = auth.RandomBytes(64);
    std::string tokenBytesHash = auth.HashBinary(tokenBytes);
    std::string tokenBase64Encoded = auth.Base64Encode(tokenBytes);
    std::string tokenUrlSafeEncoded = URLEncode(tokenBase64Encoded);
    TableHelpers::EmailVerifications emailVerifications(
        databaseHelper_, secrets);
    emailVerifications.AddEmailVerificationById(transaction, id, tokenBytesHash);
    if (sendEmail) {
        // Phase 3.3 of the security review: the verification link points
        // at the SPA (kWebsiteAddressLogin), not the API. The SPA reads
        // email + secret from the query string and immediately POSTs to
        // /api/verify. apiLinkPrefix is no longer used in the template
        // — we still pass it through for backwards compatibility with
        // the existing function signature.
        std::string spaBaseUrl = secrets->LookupSecret(
            transaction, Secrets::kWebsiteAddressLogin);
        std::string apiLinkPrefix = secrets->LookupSecret(
            transaction, Secrets::kWebsiteApiLinkPrefix);
        std::string activationLink = secrets->LookupSecret(
            transaction, Secrets::kWebsiteActivationLink);

        // Phase 1.3 componentization: brand identity for the (framework)
        // activation-mail builder comes from app-registered secrets rather than
        // being hardcoded in the template. The studio name doubles as the
        // From-header display name today; TenantBranding keeps the fields
        // separate so the tenancy project can diverge them per-tenant.
        ::Mail::TenantBranding branding;
        branding.studioName =
            secrets->LookupSecret(transaction, Secrets::kMailSenderName);
        branding.senderName = branding.studioName;
        branding.senderAddress =
            secrets->LookupSecret(transaction, Secrets::kMailSenderAddress);
        branding.websiteUrl = spaBaseUrl;

        std::string mailBody = Auth::Mail::GenerateVerifyMailBody(
            URLEncode(personInfo.email),
            tokenUrlSafeEncoded,
            personInfo.firstName,
            personInfo.lastName,
            branding,
            apiLinkPrefix,
            activationLink);
        ::Mail::MailAddress mailAddressFrom = {
            branding.senderName, branding.senderAddress };
        ::Mail::MailAddress mailAddressTo = {
            personInfo.firstName + " " + personInfo.lastName,
            personInfo.email };
        // Send verification email
        ::Mail::MailMessage message(mailAddressFrom, mailAddressTo);
        message.SetSubject(secrets->LookupSecret(
            transaction, Secrets::kMailActivationEmailSubject));
        message.SetBodyHtml(mailBody);
        mailHelper->SendMail(message);
    }
}

void PersonHelper::VerifyPersonEmail(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view emailEncoded,
    std::string_view encodedSecret) {
    TableHelpers::EmailVerifications emailVerifications(
        databaseHelper_, secrets);
    AuthHelper auth;
    std::string urlDecodedSecret = URLDecode(encodedSecret);
    Blob secretBytes = auth.Base64Decode(std::string(urlDecodedSecret));
    std::string secretHash = auth.HashBinary(secretBytes);
    std::string email = URLDecode(emailEncoded);
    // Inject the constant-time comparator: the table helper stays pure CRUD
    // and the auth-layer dependency lives here, in business_logic/auth.
    if (!emailVerifications.DoEmailVerification(
            transaction, email, secretHash,
            &AuthHelper::ConstantTimeEqual)) {
        throw std::runtime_error("PersonHelper::VerifyPersonEmail - Email verification failed.");
    }
    people_.VerifyPersonEmail(transaction, email);
}

// Create a fully validated user
void PersonHelper::CreateFullyValidatedUser(
    Transaction& transaction,
    const PersonInfo& personInfo,
    std::string_view password,
    Secrets::SecretsHelperPtr secrets) {
    std::string hash;
    if (secrets) {
        // Production / bootstrap path — Argon2id cost driven by
        // kAuthArgon2OpsLimit / kAuthArgon2MemLimitKb (MODERATE by default).
        hash = AuthHelper::HashPasswordWithSecrets(
            transaction, secrets, std::string(password));
    } else {
        // Test/no-secrets fallback. INTERACTIVE keeps the suite fast;
        // tests don't need crackproof hashes. Phase 2.5 of the security
        // review.
        AuthHelper auth;
        hash = auth.HashPassword(
            std::string(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE);
    }
    int64_t id = people_.AddPerson(
        transaction, personInfo.email, personInfo.firstName, personInfo.lastName, hash);
    people_.VerifyPersonEmail(transaction, personInfo.email);
}

bool PersonHelper::IsPerson(
    Transaction& transaction,
    std::string_view email) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    return IsVerified(person);
}

bool PersonHelper::LookupPerson(
    Transaction& transaction,
    int64_t id,
    PersonInfo& personInfo) {
    KeyValueTable person = people_.GetPersonById(transaction, id);
    if (!IsVerified(person)) {
        return false;
    }

    personInfo.email = person.at(std::string(DbSchema::kPeopleEmail));
    personInfo.firstName = person.at(std::string(DbSchema::kPeopleFirstName));
    personInfo.lastName = person.at(std::string(DbSchema::kPeopleLastName));

    return true;
}

bool PersonHelper::LookupPerson(
    Transaction& transaction,
    std::string_view email,
    PersonInfo& personInfo, int64_t& id) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }

    try {
        id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    }
    catch (...) {
        return false;
    }

    personInfo.email = person.at(std::string(DbSchema::kPeopleEmail));
    personInfo.firstName = person.at(std::string(DbSchema::kPeopleFirstName));
    personInfo.lastName = person.at(std::string(DbSchema::kPeopleLastName));

    return true;
}

bool PersonHelper::VerifyPassword(
    Transaction& transaction,
    std::string_view email,
    std::string_view password) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }

    const std::string& hash = person.at(std::string(DbSchema::kPeoplePasswordHash));
    AuthHelper auth;
    return auth.VerifyPassword(std::string(password), hash);
}

namespace {

int64_t LookupInt64SecretWithFallback(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view key,
    int64_t fallback) {
    if (!secrets) return fallback;
    std::string val = secrets->LookupSecret(transaction, key);
    if (val.empty()) return fallback;
    try {
        return std::stoll(val);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

PersonHelper::LoginAttemptResult PersonHelper::LoginPerson(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view email,
    std::string_view password) {

    LoginAttemptResult result;

    // Look up the person row. CITEXT email column means
    // LookupPersonByEmail is case-insensitive.
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        // Unknown email — return failure with no lockout side
        // effect. The 5.7 timing-padding case is handled in the
        // endpoint layer; here we just report the result.
        return result;
    }
    if (!IsVerified(person)) {
        // Email not verified — same outcome as wrong password.
        return result;
    }

    int64_t personId = -1;
    try {
        personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    } catch (...) {
        return result;
    }

    // Argon2id verify against the stored hash. This is the
    // expensive part of the login path (~250ms in production).
    const std::string& hash = person.at(std::string(DbSchema::kPeoplePasswordHash));
    AuthHelper auth;
    bool credentialsValid = auth.VerifyPassword(
        std::string(password), hash);

    int64_t threshold = LookupInt64SecretWithFallback(
        transaction, secrets,
        Secrets::kAuthAccountLockoutAfterFailures,
        /*fallback=*/10);
    int64_t lockoutDurationMicros = LookupInt64SecretWithFallback(
        transaction, secrets,
        Secrets::kAuthAccountLockoutDurationInMicros,
        /*fallback=*/1800LL * 1000000LL);  // 30 min

    if (credentialsValid) {
        // Reset bookkeeping in one statement. RETURNING gives us the
        // post-update view for the result struct.
        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "UPDATE people "
            "SET failed_login_attempts = 0, locked_until = NULL "
            "WHERE id = $1 "
            "RETURNING failed_login_attempts, "
            "          COALESCE(locked_until, 0) AS locked_until",
            std::to_string(personId));
        result.success = true;
        result.personId = personId;
        try {
            result.failedLoginAttempts = std::stoll(row.at(
                std::string(DbSchema::kPeopleFailedLoginAttempts)));
            result.lockedUntil = std::stoll(row.at(
                std::string(DbSchema::kPeopleLockedUntil)));
        } catch (...) {
            // Values stay at their zero defaults — non-fatal for
            // the result.
        }
        return result;
    }

    // Failed verify — atomically increment failed_login_attempts
    // and conditionally set locked_until. The CASE expression
    // ensures the update is one statement (no read-modify-write
    // race). RETURNING tells us whether the lock was just set.
    KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
        "UPDATE people "
        "SET failed_login_attempts = failed_login_attempts + 1, "
        "    locked_until = CASE "
        "      WHEN failed_login_attempts + 1 >= $2::bigint "
        "        THEN now_us() + $3::bigint "
        "      ELSE locked_until "
        "    END "
        "WHERE id = $1 "
        "RETURNING failed_login_attempts, "
        "          COALESCE(locked_until, 0) AS locked_until",
        std::to_string(personId),
        std::to_string(threshold),
        std::to_string(lockoutDurationMicros));

    int64_t newAttempts = 0;
    int64_t newLockedUntil = 0;
    try {
        newAttempts = std::stoll(row.at(
            std::string(DbSchema::kPeopleFailedLoginAttempts)));
        newLockedUntil = std::stoll(row.at(
            std::string(DbSchema::kPeopleLockedUntil)));
    } catch (...) {
        // Defensive — leave at 0; the lockout decision below
        // becomes inert.
    }

    result.success = false;
    result.personId = -1;
    result.failedLoginAttempts = newAttempts;
    result.lockedUntil = newLockedUntil;
    // The lock was just set iff the attempt count is exactly at
    // the threshold (a follow-up failed attempt against an already-
    // locked account also lands here, but the CASE ensures
    // locked_until isn't bumped on every subsequent failure beyond
    // the threshold). justLocked is "we transitioned across the
    // threshold this call".
    result.justLocked =
        (newAttempts == threshold) && (newLockedUntil > 0);

    return result;
}

bool PersonHelper::CreateSessionToken(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view email,
    std::string& outToken) {
    outToken.clear();
    // Lookup session max duration
    if (!secrets) {
        return false;
    }
    std::string val = secrets->LookupSecret(
        transaction, Secrets::kAuthSessionMaxDuractioninMicros);
    if (val.empty()) {
        return false;
    }
    int64_t maxDuration = std::stoll(val);

    // Lookup verified person
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }
    int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    // Create session
    TableHelpers::Sessions sessions(databaseHelper_);
    int64_t sessionId = sessions.AddSession(transaction, personId, maxDuration);

    // Fetch uuid to return token
    KeyValueTable row = sessions.LookupSessionById(transaction, sessionId);
    auto it = row.find(std::string(DbSchema::kSessionsUuid));
    if (it == row.end()) {
        return false;
    }
    outToken = it->second;
    return true;
}

void PersonHelper::SessionUsed(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    TransactionProviderPtr transactionProvider,
    std::string_view sessionToken) {
    // Find session by UUID
    TableHelpers::Sessions sessions(databaseHelper_);
    KeyValueTable row = sessions.LookupSessionByUuid(
        transaction, sessionToken);
    if (row.empty()) {
        return;
    }

    int64_t sessionId = 0;
    int64_t lastSeenUs = 0;
    sessionId = std::stoll(row.at(std::string(DbSchema::kSessionsId)));
    lastSeenUs = std::stoll(row.at(std::string(DbSchema::kSessionsLastSeenAt)));

    if (!secrets) {
        return;
    }
    std::string windowStr = secrets->LookupSecret(
        transaction, Secrets::kAuthSessionLastSeenUpdateDurationInMicros);
    if (windowStr.empty()) {
        return;
    }

    int64_t windowUs = std::stoll(windowStr);

    int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT now_us()"));

    if (nowUs <= lastSeenUs + windowUs) {
        return; // Not past threshold; skip update
    }

    // Defer UpdateLastSeen in a background transaction
    DatabaseHelper dbh = databaseHelper_;
    auto provider = transactionProvider;
    ThreadPool::GetInstance().Queue([provider, dbh, sessionId]() {
        if (!provider) return;
        provider->RunInTransaction([dbh, sessionId](Transaction& t) {
            TableHelpers::Sessions sess(dbh);
            sess.UpdateLastSeen(t, sessionId);
            });
        });
}

bool PersonHelper::LookupPersonIdBySessionToken(
    Transaction& transaction,
    std::string_view sessionToken,
    int64_t& outPersonId) {
    // Lookup session by uuid, ensure not expired and not revoked
    TableHelpers::Sessions sessions(databaseHelper_);
    KeyValueTable row = sessions.LookupSessionByUuid(transaction, sessionToken);
    if (row.empty()) {
        return false;
    }
    int64_t nowUs = std::stoll(transaction.RunSqlStatementReturningOneValue(
        "SELECT now_us()"));

    const std::string& personIdStr = row.at(std::string(DbSchema::kSessionsPersonId));
    const std::string& expiresAtStr = row.at(std::string(DbSchema::kSessionsExpiresAt));
    const std::string& revokedStr = row.at(std::string(DbSchema::kSessionsRevoked));

    if (StringToBool(revokedStr)) {
        return false;
    }
    int64_t expiresAt = 0;
    try { expiresAt = std::stoll(expiresAtStr); }
    catch (...) { return false; }
    if (nowUs > expiresAt) {
        return false;
    }
    try { outPersonId = std::stoll(personIdStr); }
    catch (...) { return false; }
    return true;
}

bool PersonHelper::CreateDeviceToken(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view email,
    std::string& outTokenId,
    std::string& outBase64EncodedSecret) {
    outTokenId.clear();
    outBase64EncodedSecret.clear();

    // Lookup verified person
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }

    try {
        int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

        std::string hashHex;
        int64_t expiresUs;
        if(!DeviceTokenHelper(
            transaction, secrets, hashHex, outBase64EncodedSecret, expiresUs)) {
            return false;
        }

        // Insert device token
        TableHelpers::DeviceTokens deviceTokens(databaseHelper_);
        int64_t id = deviceTokens.AddDeviceToken(transaction, personId, hashHex, expiresUs);

        // Lookup row to fetch UUID
        KeyValueTable row = deviceTokens.LookupDeviceTokenById(transaction, id);
        auto it = row.find(std::string(DbSchema::kDeviceTokensUuid));
        if (it == row.end()) {
            return false;
        }
        outTokenId = it->second;

        return true;
    } catch (...) {
        return false;
    }
}

bool PersonHelper::TryLoginWithDeviceToken(
    Transaction& transaction,
    Secrets::SecretsHelperPtr secrets,
    std::string_view tokenId,
    std::string_view base64EncodedSecret,
    std::string& outTokenId,
    std::string& outBase64EncodedSecret) {
    // Phase 2.4 of the security review: this used to be a sequence of
    // SELECT, UUID-compare, IsValid, then UPDATE — three round-trips
    // with a TOCTOU race in between. ConsumeAndRotate collapses the
    // validate + rotate into a single conditional UPDATE that matches
    // on (secret_hash, uuid, NOT revoked, now < expires) and returns
    // the row's person_id plus the new uuid in one shot.

    outTokenId.clear();
    outBase64EncodedSecret.clear();

    try {
        // Decode and hash the secret the client presented.
        AuthHelper auth;
        Blob secretBytes = auth.Base64Decode(std::string(base64EncodedSecret));
        std::string oldSecretHash = auth.HashBinary(secretBytes);

        // Mint a replacement secret + hash + expiration up front so the
        // UPDATE has everything it needs.
        std::string newSecretHash;
        int64_t newExpiresUs = 0;
        if (!DeviceTokenHelper(
            transaction, secrets, newSecretHash, outBase64EncodedSecret, newExpiresUs)) {
            return false;
        }

        TableHelpers::DeviceTokens deviceTokens(databaseHelper_);
        int64_t personId = 0;
        if (!deviceTokens.ConsumeAndRotate(
                transaction,
                oldSecretHash,
                tokenId,
                newSecretHash,
                newExpiresUs,
                personId,
                outTokenId)) {
            // Rotation failed — bad secret hash, bad uuid, revoked, or
            // expired. Don't expose which.
            outBase64EncodedSecret.clear();
            return false;
        }
        return true;
    }
    catch (...) {
        outTokenId.clear();
        outBase64EncodedSecret.clear();
        return false;
    }
}

bool PersonHelper::UpdateInfo(
    Transaction& transaction,
    std::string_view email, 
    const PersonInfo& personInfo) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }
    int64_t id = 0;
    try {
        id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    } catch (...) {
        return false;
    }

    KeyValueTable changes;
    if (!personInfo.email.empty())     changes[std::string(DbSchema::kPeopleEmail)] = personInfo.email;
    if (!personInfo.firstName.empty()) changes[std::string(DbSchema::kPeopleFirstName)] = personInfo.firstName;
    if (!personInfo.lastName.empty())  changes[std::string(DbSchema::kPeopleLastName)] = personInfo.lastName;

    if (changes.empty()) return false; // nothing to update
    people_.UpdatePerson(transaction, id, changes);
    return true;
}

bool PersonHelper::UpdatePassword(
    Transaction& transaction,
    std::string_view email,
    std::string_view password,
    Secrets::SecretsHelperPtr secrets) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) {
        return false;
    }
    int64_t id = 0;
    try {
        id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    } catch (...) {
        return false;
    }

    std::string hash;
    if (secrets) {
        hash = AuthHelper::HashPasswordWithSecrets(
            transaction, secrets, std::string(password));
    } else {
        AuthHelper auth;
        hash = auth.HashPassword(
            std::string(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE);
    }
    people_.UpdatePassword(transaction, id, hash);
    return true;
}

void PersonHelper::DeletePerson(
    Transaction& transaction,
    std::string_view email) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (!IsVerified(person)) return;
    try {
        int64_t id = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
        people_.DeletePerson(transaction, id);
    } catch (...) {
    }
}

void PersonHelper::LogoutPerson(
    Transaction& transaction,
    int64_t personId) {
    // Remove all device tokens and sessions for the given user.
    TableHelpers::DeviceTokens deviceTokens(databaseHelper_);
    deviceTokens.RemoveDeviceTokensForUser(transaction, personId);

    TableHelpers::Sessions sessions(databaseHelper_);
    sessions.RemoveSessionsForUser(transaction, personId);
}

// Role-based helpers
StringArray PersonHelper::GetRolesForUser(Transaction& transaction, int64_t personId) {
    StringArray rolesOut;
    // Validate person exists
    KeyValueTable person = people_.GetPersonById(transaction, personId);
    if (!IsVerified(person)) {
        throw std::runtime_error("PersonHelper::GetRolesForUser - person not found");
    }

    TableHelpers::RoleAssignments ra(databaseHelper_);
    KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(transaction, personId);

    TableHelpers::Roles roles(databaseHelper_);
    for (const auto& kv : assignments) {
        int64_t roleId = std::stoll(kv.at(std::string(DbSchema::kRoleAssignmentsRoleId)));
        KeyValueTable roleRow = roles.GetRole(transaction, roleId);
        rolesOut.push_back(roleRow.at(std::string(DbSchema::kRolesName)));
    }
    return rolesOut;
}

StringArray PersonHelper::GetPermissionsForUser(Transaction& transaction, int64_t personId) {
    StringArray permsOut;
    // Validate person exists
    KeyValueTable person = people_.GetPersonById(transaction, personId);
    if (!IsVerified(person)) {
        throw std::runtime_error("PersonHelper::GetPermissionsForUser - person not found");
    }

    // Build a set to avoid duplicates
    StringSet uniquePerms;

    TableHelpers::RoleAssignments ra(databaseHelper_);
    KeyValueTableArray assignments = ra.GetRoleAssignmentsForPerson(transaction, personId);

    TableHelpers::RolePermissions rperms(databaseHelper_);
    TableHelpers::Permissions perms(databaseHelper_);

    for (const auto& kv : assignments) {
        int64_t roleId = std::stoll(kv.at(std::string(DbSchema::kRoleAssignmentsRoleId)));
        KeyValueTableArray rpRows = rperms.GetRolePermissionsForRole(transaction, roleId);
        for (const auto& rp : rpRows) {
            int64_t permId = std::stoll(rp.at(std::string(DbSchema::kRolePermissionsPermissionId)));
            KeyValueTable pRow = perms.GetPermission(transaction, permId);
            uniquePerms.insert(pRow.at(std::string(DbSchema::kPermissionsName)));
        }
    }

    for (const auto& name : uniquePerms) {
        permsOut.push_back(name);
    }
    return permsOut;
}

}  // namespace Auth {