#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/table_helpers/people.h"
#include "util/mail/mail_helper.h"
#include "util/secrets/secrets_helper.h"

namespace Auth {

// Outcome of EnsureAccountWithWelcome. `firstName`/`lastName` are the
// resolved names — the stored ones when an existing account was reused.
struct QuickAccountResult {
    int64_t personId = 0;
    bool alreadyExists = false;
    std::string firstName;
    std::string lastName;
};

// Optional callback invoked after a NEW quick account is created (never on the
// reuse-existing path), inside the same transaction, before the welcome email
// is sent. The app uses this seam to run app-specific post-creation side
// effects — currently converting pending gift-permission invitations — so that
// auth (framework) no longer has to include app modules like
// business_logic/payment. Wire it with Payment::MakeGiftInvitationHook.
//
// Exceptions thrown by the hook are caught and swallowed: a post-creation side
// effect must never roll back or block the account creation itself (this
// preserves the pre-refactor behavior where gift-invitation processing failures
// were ignored).
using QuickAccountPostCreateHook = std::function<
    void(Transaction& transaction, int64_t personId, const std::string& email)>;

// Staff-initiated account creation ("quick accounts"): walk-in check-in
// (Classes Phase 8) and the staff check-in page's quick-account flow. Creates
// a fully-validated account with a random temporary password, marks it
// must_change_password, processes pending gift-permission invitations, and
// emails the person their temporary password so they can log in and set
// their own. Reuses an existing account by email instead of duplicating.
class QuickAccountHelper {
public:
    // `secretsHelper` controls Argon2id hash cost (null → fast test-cost) and
    // is needed for gift-invitation processing. `mailHelper` null skips the
    // welcome email and invitation processing — test/bootstrap contexts only;
    // production callers must pass both or the person never learns their
    // temporary password.
    // `postCreateHook` (optional) runs after a new account is created — see
    // QuickAccountPostCreateHook. Null skips it (test/bootstrap contexts, or
    // callers with no post-creation side effects).
    QuickAccountHelper(
        DatabaseHelper databaseHelper,
        Secrets::SecretsHelperPtr secretsHelper,
        ::Mail::MailHelperPtr mailHelper,
        QuickAccountPostCreateHook postCreateHook = nullptr);

    // Reuse the account with this email, or create one (temporary password +
    // must_change_password + welcome email). Names must be non-empty for the
    // create path; the caller validates.
    QuickAccountResult EnsureAccountWithWelcome(
        Transaction& transaction,
        const std::string& firstName,
        const std::string& lastName,
        const std::string& email);

private:
    DatabaseHelper databaseHelper_;
    Secrets::SecretsHelperPtr secretsHelper_;
    ::Mail::MailHelperPtr mailHelper_;
    QuickAccountPostCreateHook postCreateHook_;
    TableHelpers::People people_;
};

}  // namespace Auth
