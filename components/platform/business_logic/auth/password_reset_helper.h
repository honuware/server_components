#pragma once

#include <cstdint>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/table_helpers/password_reset_tokens.h"
#include "sql_util/table_helpers/people.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secrets_helper.h"

namespace Auth {

// How CompleteReset ended. Every way a link can be bad — no such account, no
// link, a wrong secret, an expired link, one out of tries — is the single
// InvalidLink, so a caller can't tell them apart.
enum class PasswordResetOutcome {
    Success,
    InvalidLink,
    // The new password breaks the rules registration enforces (today: it is
    // empty). Checked before the link is touched, so it costs no attempt.
    PasswordRejected,
};

struct PasswordResetResult {
    PasswordResetOutcome outcome = PasswordResetOutcome::InvalidLink;
    // The account that was reset; 0 unless the outcome is Success.
    int64_t personId = 0;
};

// Finishing website Phase 11.3: "forgot password". A request mails a one-use
// link; following it sets a new password. The safeguards (plan D13):
//   - the same answer whether or not the address has an account;
//   - only a hash of the link's secret is stored;
//   - a link lasts Secrets::kAuthPasswordResetTtlMinutes (default 60) and a
//     new request replaces it;
//   - every wrong secret counts, and the kAttemptLimit-th deletes the link;
//   - a reset ends every session and remember-me token, clears the
//     must-change flag and the lockout, and verifies the email.
// The request caps are LoginGate::CheckBeforeForgotPassword's job.
class PasswordResetHelper {
public:
    static constexpr int64_t kAttemptLimit = 5;

    PasswordResetHelper(DatabaseHelper databaseHelper);
    PasswordResetHelper(const PasswordResetHelper&) = default;
    PasswordResetHelper& operator=(const PasswordResetHelper&) = default;
    ~PasswordResetHelper() = default;

    // Starts a reset for `email` and returns the account's person id, or 0
    // when no account has that address — in which case nothing is stored and
    // nothing is sent. A known address gets a new link (replacing any earlier
    // one) and the reset mail.
    //
    // All the SQL runs here, synchronously; the mail itself is QUEUED on the
    // ThreadPool, so the caller's response takes the same time whether or not
    // the account exists. The queued task never touches the database, so the
    // caller may still run SQL afterwards. A null `mail` stores the link and
    // sends nothing.
    int64_t RequestReset(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        ::Mail::MailHelperPtr mail,
        std::string_view email);

    // Sets `newPassword` if `secret` is the live link for `email`.
    PasswordResetResult CompleteReset(
        Transaction& transaction,
        Secrets::SecretsHelperPtr secrets,
        std::string_view email,
        std::string_view secret,
        std::string_view newPassword);

private:
    DatabaseHelper databaseHelper_;
    TableHelpers::People people_;
    TableHelpers::PasswordResetTokens tokens_;
};

}  // namespace Auth
