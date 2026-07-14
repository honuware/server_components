#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sql_util/table_helpers/people.h"
#include "sql_util/database_access/transaction_provider.h"

namespace Secrets {

class SecretsHelper;
using SecretsHelperPtr = std::shared_ptr<SecretsHelper>;

}  // namespace Secrets {

namespace Mail {

class MailHelper;
using MailHelperPtr = std::shared_ptr<MailHelper>;

}

namespace Auth {

struct PersonInfo {
    std::string email;
    std::string firstName;
    std::string lastName;
};

class PersonHelper {
public:
    PersonHelper(
        DatabaseHelper databaseHelper);
    PersonHelper() = delete;
    PersonHelper(const PersonHelper&) = default;
    PersonHelper& operator=(const PersonHelper&) = default;
    ~PersonHelper() = default;

    // Email validation workflow
    void PreliminaryRegisterPerson(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        Mail::MailHelperPtr mailHelper,
        const PersonInfo& personInfo, 
        std::string_view password, 
        bool sendEmail = true);
    void VerifyPersonEmail(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string_view encodedSecret);

    // Create a fully validated user. The optional `secrets` argument
    // controls Argon2id cost: production callers should pass their
    // SecretsHelper so the hash uses kAuthArgon2OpsLimit /
    // kAuthArgon2MemLimitKb (defaults MODERATE). Test/bootstrap callers
    // that don't supply secrets get a fast INTERACTIVE-cost hash so the
    // suite stays quick. Phase 2.5 of the security review.
    void CreateFullyValidatedUser(
        Transaction& transaction,
        const PersonInfo& personInfo,
        std::string_view password,
        Secrets::SecretsHelperPtr secrets = nullptr);

    bool IsPerson(
        Transaction& transaction,
        std::string_view email);
    bool LookupPerson(
        Transaction& transaction,
        int64_t id,
        PersonInfo& personInfo);
    bool LookupPerson(
        Transaction& transaction,
        std::string_view email,
        PersonInfo& personInfo,
        int64_t& id);
    bool VerifyPassword(
        Transaction& transaction,
        std::string_view email,
        std::string_view password);

    // Phase 5.3 of the security review: the high-level login path
    // that callers (the /api/login endpoint, mostly) should use
    // instead of VerifyPassword directly.
    //
    // Behavior:
    //   1. Looks up the person row.
    //   2. Verifies the submitted password against the stored
    //      Argon2id hash.
    //   3. Atomically updates `people.failed_login_attempts` and
    //      `people.locked_until`:
    //        - On success: failed_login_attempts = 0, locked_until = NULL
    //        - On failure: failed_login_attempts += 1 (and
    //          locked_until = now_us() + lockout_duration when the
    //          new count crosses the threshold)
    //   4. Returns a result struct describing the outcome.
    //
    // The lockout UPDATE uses a single atomic statement, so two
    // parallel attempts at the threshold can over-count by at most
    // one (acceptable). The lockout is committed by the time the
    // endpoint returns.
    //
    // VerifyPassword (above) remains for callers that don't want
    // the lockout side effect — e.g. tests that need to assert on
    // hash semantics directly. Endpoints in the live auth path go
    // through LoginPerson.
    struct LoginAttemptResult {
        // True iff the password matched AND the account was not
        // locked at the time of the attempt.
        bool success = false;
        // True iff this attempt's failure pushed the count over
        // the threshold and just set locked_until. Used to drive
        // the admin_alerts row insertion.
        bool justLocked = false;
        // Populated on success; -1 otherwise. Lookups go through
        // people.email which is CITEXT, so case-insensitive.
        int64_t personId = -1;
        // Post-update bookkeeping (for telemetry).
        int64_t failedLoginAttempts = 0;
        // 0 when the account is not locked.
        int64_t lockedUntil = 0;
    };
    LoginAttemptResult LoginPerson(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string_view password);
    bool CreateSessionToken(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string& outToken);
    void SessionUsed(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        TransactionProviderPtr transactionProvider,
        std::string_view sessionToken);
    bool LookupPersonIdBySessionToken(
        Transaction& transaction,
        std::string_view sessionToken,
        int64_t& outPersonId);
    bool CreateDeviceToken(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string& outTokenId,
        std::string& outBase64EncodedSecret);
    bool TryLoginWithDeviceToken(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view tokenId,
        std::string_view base64EncodedSecret,
        std::string& outTokenId,
        std::string& outBase64EncodedSecret);
    // Leave strings empty that aren't being updated
    bool UpdateInfo(
        Transaction& transaction,
        std::string_view email, 
        const PersonInfo& personInfo);
    // The optional `secrets` argument controls Argon2id cost — see
    // CreateFullyValidatedUser. Production callers (the
    // /api/update_user_password endpoint) pass their SecretsHelper.
    // Phase 2.5 of the security review.
    bool UpdatePassword(
        Transaction& transaction,
        std::string_view email,
        std::string_view password,
        Secrets::SecretsHelperPtr secrets = nullptr);
    void DeletePerson(
        Transaction& transaction,
        std::string_view email);
    void LogoutPerson(
        Transaction& transaction,
        int64_t personId);

    // Role-based helpers
    StringArray GetRolesForUser(Transaction& transaction, int64_t personId);
    StringArray GetPermissionsForUser(Transaction& transaction, int64_t personId);

private:
    DatabaseHelper databaseHelper_;
    TableHelpers::People people_;
};

}  // namespace Auth {