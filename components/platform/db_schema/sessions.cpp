#include "sessions.h"

#include "people.h"

namespace DbSchema {

	void MakeSessionsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSessionsTable);
		databaseInfo.AddColumnPrimaryKey(
			kSessionsTable,
			kSessionsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSessionsTable,
			kSessionsUuid,
			DB_TYPE_UUID,
			kDatabaseInfoDefaultGenRandomUuid);
		databaseInfo.AddColumnForeignKeyRef(
			kPeopleTable,
			kPeopleId,
			kSessionsTable,
			kSessionsPersonId);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSessionsTable,
			kSessionsCreatedAt,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSessionsTable,
			kSessionsLastSeenAt,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnSimple(
			kSessionsTable,
			kSessionsExpiresAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSessionsTable,
			kSessionsRevoked,
			DB_TYPE_BOOL,
			kDatabaseInfoDefaultFalse);
		// Lookups by uuid happen on every authenticated request — without
		// uniqueness + an index the lookup degrades to a sequential scan as
		// the table grows. Phase 1.1 of the security review.
		databaseInfo.AddUniqueConstraint(kSessionsTable, kSessionsUuid);
	}

}  // namespace DbSchema