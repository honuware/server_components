#include "email_verifications.h"

#include <stdexcept>
#include <sstream>

#include "db_schema/email_verifications.h"
#include "db_schema/people.h"
#include "db_schema/admin_alerts.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/secrets/secret_keys.h"

namespace TableHelpers {

namespace {
constexpr const char* kSelectExpiredSql =
    "SELECT id FROM email_verifications "
    "WHERE expires_at < (extract(epoch FROM now()) * 1000000)::bigint "
    "ORDER BY id ASC";

constexpr const char* kSelectAttemptsOverSql =
    "SELECT id FROM email_verifications "
    "WHERE attempts > $1 ORDER BY id ASC";
} // namespace

EmailVerifications::EmailVerifications(
    DatabaseHelper databaseHelper,
    Secrets::SecretsHelperPtr secretsHelper)
    : databaseHelper_(databaseHelper),
      secretsHelper_(std::move(secretsHelper)),
      people_(databaseHelper_),
      adminAlerts_(databaseHelper_) {
}

int64_t EmailVerifications::CurrentTimeMicros(Transaction& transaction) const {
    std::string micros = transaction.RunSqlStatementReturningOneValue(
        "SELECT now_us()");
    return std::stoll(micros);
}

int64_t EmailVerifications::LookupExpirationWindowMicros(
    Transaction& transaction) const {
    if (!secretsHelper_) {
        throw std::runtime_error(
            "EmailVerifications::LookupExpirationWindowMicros - secretsHelper is null");
    }
    std::string val =
        secretsHelper_->LookupSecret(transaction,
            Secrets::kEmailVerificationExpirationWindowInMicros);
    if (val.empty()) {
        throw std::runtime_error(
            "EmailVerifications::LookupExpirationWindowMicros - secret empty");
    }
    return std::stoll(val);
}

int EmailVerifications::LookupAttemptLimit(
    Transaction& transaction) const {
    if (!secretsHelper_) {
        throw std::runtime_error(
            "EmailVerifications::LookupAttemptLimit - secretsHelper is null");
    }
    std::string val =
        secretsHelper_->LookupSecret(transaction,
            Secrets::kEmailVerificationAttemptLimit);
    if (val.empty()) {
        throw std::runtime_error(
            "EmailVerifications::LookupAttemptLimit - secret empty");
    }
    return std::stoi(val);
}

int64_t EmailVerifications::AddEmailVerificationByEmail(
    Transaction& transaction,
    std::string_view email,
    std::string_view tokenHash) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        throw std::runtime_error(
            "EmailVerifications::AddEmailVerificationByEmail - person not found");
    }
    int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    return AddEmailVerificationById(transaction, personId, tokenHash);
}

int64_t EmailVerifications::AddEmailVerificationById(
    Transaction& transaction,
    int64_t personId,
    std::string_view tokenHash) {
    // Only one pending verification per person. If one already exists (e.g.
    // the user clicked "resend"), drop it in the same transaction so the
    // new token replaces the old. The schema-level UNIQUE(person_id)
    // constraint catches any caller that bypasses this helper.
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kEmailVerificationsTable,
        DbSchema::kEmailVerificationsPersonId,
        StringFromInt(personId));

    int64_t nowMicros = CurrentTimeMicros(transaction);
    int64_t windowMicros = LookupExpirationWindowMicros(transaction);
    int64_t expiresAt = nowMicros + windowMicros;
    KeyValueTable kv = {
        { std::string(DbSchema::kEmailVerificationsPersonId), StringFromInt(personId) },
        { std::string(DbSchema::kEmailVerificationsTokenHash), std::string(tokenHash) },
        { std::string(DbSchema::kEmailVerificationsExpiresAt), StringFromInt(expiresAt) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kEmailVerificationsTable,
        kv);
}

StringArray EmailVerifications::ListExpiredEmailVerifications(
    Transaction& transaction) {
    KeyValueTableArray keyValueTableArray = transaction.RunSqlStatementReturningKeyValueTableArray(
        kSelectExpiredSql);
    StringArray ids;
    for(size_t i = 0; i < keyValueTableArray.size(); ++i) {
        ids.push_back(
            keyValueTableArray[i].at(std::string(DbSchema::kEmailVerificationsId)));
    }
    return ids;
}

void EmailVerifications::DeleteEmailVerification(
    Transaction& transaction,
    int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kEmailVerificationsTable,
        DbSchema::kEmailVerificationsId,
        StringFromInt(id));
}

int64_t EmailVerifications::DeleteExpired(Transaction& transaction, int64_t asOfUs) {
    KeyValueTableArray deleted = transaction.RunSqlStatementReturningKeyValueTableArray(
        "DELETE FROM email_verifications WHERE expires_at < $1 RETURNING id",
        StringFromInt(asOfUs));
    return static_cast<int64_t>(deleted.size());
}

StringArray EmailVerifications::GetEmailVerificationsOverNumberOfAttempts(
    Transaction& transaction,
    int numberOfAttempts) {
    KeyValueTableArray keyValueTableArray = transaction.RunSqlStatementReturningKeyValueTableArray(
        kSelectAttemptsOverSql, StringFromInt(numberOfAttempts));
    StringArray ids;
    for(size_t i = 0; i < keyValueTableArray.size(); ++i) {
        ids.push_back(
            keyValueTableArray[i].at(std::string(DbSchema::kEmailVerificationsId)));
    }
    return ids;
}

KeyValueTable EmailVerifications::GetEmailVerificationInfo(
    Transaction& transaction,
    int64_t id) {
    KeyValueTable kv = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kEmailVerificationsTable,
        DbSchema::kEmailVerificationsId,
        StringFromInt(id));
    if (kv.empty()) {
        throw std::runtime_error(
            "EmailVerifications::GetEmailVerificationInfo - record not found");
    }
    return kv;
}

KeyValueTable EmailVerifications::GetEmailVerificationInfoByEmail(
    Transaction& transaction,
    std::string_view email) {
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        throw std::runtime_error(
            "EmailVerifications::GetEmailVerificationInfoByEmail - person not found");
    }
    int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    KeyValueTable kv = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kEmailVerificationsTable,
        DbSchema::kEmailVerificationsPersonId,
        StringFromInt(personId));
    if (kv.empty()) {
        throw std::runtime_error(
            "EmailVerifications::GetEmailVerificationInfoByEmail - verification not found");
    }
    return kv;
}

bool EmailVerifications::DoEmailVerification(
    Transaction& transaction,
    std::string_view email,
    std::string_view tokenHash,
    const TokenComparator& compareTokens) {
    // Phase 2.2 + 2.3 of the security review:
    //
    //   * The original SELECT-then-UPDATE flow had a TOCTOU window — two
    //     concurrent attempts could both read the same `attempts` value
    //     and both bump the counter, exceeding the limit by one. We
    //     replace it with a single conditional UPDATE that increments
    //     `attempts` atomically and only matches a row that's still
    //     within the limit and not yet expired.
    //
    //   * The token-hash comparison happens AFTER the UPDATE has claimed
    //     an attempt slot, using the caller-injected constant-time
    //     comparator (compareTokens) so the compare doesn't leak byte
    //     positions via timing. Injecting it keeps this table helper free
    //     of any business_logic/auth dependency.
    //
    //   * The DELETE on success runs in the same transaction as the
    //     UPDATE so an attacker who races a successful verification
    //     against an out-of-band failed attempt can't observe the row
    //     after it's been consumed.
    KeyValueTable person = people_.LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        throw std::runtime_error(
            "EmailVerifications::DoEmailVerification - person not found");
    }
    int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
    int attemptLimit = LookupAttemptLimit(transaction);

    // Conditional UPDATE: matches only if the row is still under the limit
    // and not yet expired. RETURNING gives us back the data we need to
    // (a) compare hashes in constant time, (b) decide whether to alert,
    // and (c) DELETE on success.
    constexpr std::string_view kClaimAttemptSql =
        "UPDATE email_verifications "
        "SET attempts = attempts + 1 "
        "WHERE person_id = $1 "
        "  AND attempts < $2 "
        "  AND expires_at > now_us() "
        "RETURNING id, token_hash, attempts";

    KeyValueTableArray rows = transaction.RunSqlStatementReturningKeyValueTableArray(
        kClaimAttemptSql,
        StringFromInt(personId),
        StringFromInt(attemptLimit));

    if (rows.empty()) {
        // Either no verification row, or the row exists but it's
        // already over the limit / expired. Either way the caller fails
        // verification. Don't leak which.
        return false;
    }

    // We claimed an attempt slot. Now check the hash in constant time.
    const KeyValueTable& row = rows[0];
    int64_t id = std::stoll(
        row.at(std::string(DbSchema::kEmailVerificationsId)));
    const std::string& storedTokenHash = row.at(
        std::string(DbSchema::kEmailVerificationsTokenHash));
    int newAttempts = std::stoi(
        row.at(std::string(DbSchema::kEmailVerificationsAttempts)));

    if (compareTokens(storedTokenHash, tokenHash)) {
        // Successful verification — consume the row in the same
        // transaction so a concurrent caller can't replay it.
        DeleteEmailVerification(transaction, id);
        return true;
    }

    // Failed compare. The attempt counter has already been incremented
    // by the UPDATE above; just fire the alert if we've now hit the
    // ceiling so admins can investigate.
    if (newAttempts >= attemptLimit) {
        std::stringstream ss;
        ss << "Email verification attempt limit reached for person_id="
           << personId << " email=" << email;
        adminAlerts_.AddAdminAlert(transaction, ss.str());
    }

    return false;
}

} // namespace TableHelpers