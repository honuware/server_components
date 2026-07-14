#include "auth_events.h"

namespace DbSchema {

void MakeAuthEventsTable(DatabaseInfo databaseInfo) {
	databaseInfo.AddTable(kAuthEventsTable);
	databaseInfo.AddColumnPrimaryKey(
		kAuthEventsTable, kAuthEventsId, DB_TYPE_BIGSERIAL);
	databaseInfo.AddColumnSimple(
		kAuthEventsTable, kAuthEventsKind, DB_TYPE_STRING);
	// Nullable person_id — login_failure on unknown email leaves it
	// NULL; a later forensic query joins ON person_id for the
	// per-user view and uses the `kind`+detail_json email for the
	// rest.
	databaseInfo.AddColumnNullable(
		kAuthEventsTable, kAuthEventsPersonId, DB_TYPE_BIGINT);
	databaseInfo.AddColumnSimple(
		kAuthEventsTable, kAuthEventsIp, DB_TYPE_STRING);
	databaseInfo.AddColumnSimple(
		kAuthEventsTable, kAuthEventsUserAgent, DB_TYPE_STRING);
	databaseInfo.AddColumnSimple(
		kAuthEventsTable, kAuthEventsWhenUs, DB_TYPE_BIGINT);
	databaseInfo.AddColumnSimple(
		kAuthEventsTable, kAuthEventsDetailJson, DB_TYPE_STRING);
}

void CreateAuthEventsIndexes(Transaction& transaction) {
	transaction.RunSqlStatement(
		"CREATE INDEX IF NOT EXISTS auth_events_person_when_idx "
		"ON auth_events (person_id, when_us DESC)");
	transaction.RunSqlStatement(
		"CREATE INDEX IF NOT EXISTS auth_events_kind_when_idx "
		"ON auth_events (kind, when_us DESC)");
}

}  // namespace DbSchema
