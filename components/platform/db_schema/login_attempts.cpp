#include "login_attempts.h"

namespace DbSchema {

void MakeLoginAttemptsTable(DatabaseInfo databaseInfo) {
	databaseInfo.AddTable(kLoginAttemptsTable);
	databaseInfo.AddColumnPrimaryKey(
		kLoginAttemptsTable, kLoginAttemptsId, DB_TYPE_BIGSERIAL);
	databaseInfo.AddColumnSimple(
		kLoginAttemptsTable, kLoginAttemptsEmailLower, DB_TYPE_CITEXT);
	databaseInfo.AddColumnSimple(
		kLoginAttemptsTable, kLoginAttemptsIp, DB_TYPE_STRING);
	databaseInfo.AddColumnSimple(
		kLoginAttemptsTable, kLoginAttemptsAttemptedAt, DB_TYPE_BIGINT);
	databaseInfo.AddColumnSimple(
		kLoginAttemptsTable, kLoginAttemptsSuccess, DB_TYPE_BOOL);
	databaseInfo.AddColumnSimple(
		kLoginAttemptsTable, kLoginAttemptsKind, DB_TYPE_STRING);
}

void CreateLoginAttemptsIndexes(Transaction& transaction) {
	transaction.RunSqlStatement(
		"CREATE INDEX IF NOT EXISTS login_attempts_email_kind_at_idx "
		"ON login_attempts (email_lower, kind, attempted_at DESC)");
	transaction.RunSqlStatement(
		"CREATE INDEX IF NOT EXISTS login_attempts_ip_kind_at_idx "
		"ON login_attempts (ip, kind, attempted_at DESC)");
	// Retention/purge index — the periodic cleanup job (Phase 5.8)
	// runs `DELETE FROM login_attempts WHERE attempted_at < $cutoff`.
	transaction.RunSqlStatement(
		"CREATE INDEX IF NOT EXISTS login_attempts_attempted_at_idx "
		"ON login_attempts (attempted_at)");
}

}  // namespace DbSchema
