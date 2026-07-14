#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"
#include "util/secrets/secrets_helper.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/admin_alerts.h"

namespace TableHelpers {

class EmailVerifications
{
public:
    // Compares a stored token hash against a presented one. Injected by the
    // caller (business_logic/auth passes AuthHelper::ConstantTimeEqual) so this
    // table helper stays pure CRUD and does not depend on the auth layer — see
    // DoEmailVerification. Contract: MUST compare the full byte length in
    // constant time; returning early on the first differing byte would leak the
    // token via timing.
    using TokenComparator =
        std::function<bool(std::string_view stored, std::string_view presented)>;

    EmailVerifications(
        DatabaseHelper databaseHelper,
        Secrets::SecretsHelperPtr secretsHelper);
    EmailVerifications(const EmailVerifications&) = default;
    EmailVerifications& operator=(const EmailVerifications&) = default;
    ~EmailVerifications() = default;

    int64_t AddEmailVerificationByEmail(
        Transaction& transaction,
        std::string_view email,
        std::string_view tokenHash);
    int64_t AddEmailVerificationById(
        Transaction& transaction,
        int64_t personId,
        std::string_view tokenHash);

    StringArray ListExpiredEmailVerifications(Transaction& transaction);
    void DeleteEmailVerification(Transaction& transaction, int64_t id);

    // Deletes all email verifications whose expires_at is strictly less than asOfUs.
    // Returns the count of rows deleted.
    int64_t DeleteExpired(Transaction& transaction, int64_t asOfUs);
    StringArray GetEmailVerificationsOverNumberOfAttempts(
        Transaction& transaction,
        int numberOfAttempts);

    KeyValueTable GetEmailVerificationInfo(
        Transaction& transaction,
        int64_t id);
    KeyValueTable GetEmailVerificationInfoByEmail(
        Transaction& transaction,
        std::string_view email);

    // Atomically claims an attempt slot and, if the presented token matches the
    // stored hash (per compareTokens), consumes the verification row. The
    // caller supplies compareTokens — this keeps the layering one-directional
    // (table helper never reaches up into business_logic/auth).
    bool DoEmailVerification(
        Transaction& transaction,
        std::string_view email,
        std::string_view tokenHash,
        const TokenComparator& compareTokens);

private:
    int64_t LookupExpirationWindowMicros(Transaction& transaction) const;
    int LookupAttemptLimit(Transaction& transaction) const;
    int64_t CurrentTimeMicros(Transaction& transaction) const;

    DatabaseHelper databaseHelper_;
    Secrets::SecretsHelperPtr secretsHelper_;
    People people_;
    AdminAlerts adminAlerts_;
};

} // namespace TableHelpers