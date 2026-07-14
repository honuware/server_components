#include "people.h"

namespace DbSchema {

	void MakePeopleTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kPeopleTable);
		databaseInfo.AddColumnPrimaryKey(
			kPeopleTable,
			kPeopleId,
			DB_TYPE_BIGSERIAL);
		// CITEXT so the unique constraint and every email lookup are
		// case-insensitive at the DB level. No per-call lowercasing
		// discipline required. Phase 1.6 of the security review.
		databaseInfo.AddColumnUnique(
			kPeopleTable,
			kPeopleEmail,
			DB_TYPE_CITEXT);
		databaseInfo.AddColumnSimple(
			kPeopleTable, 
			kPeopleFirstName, 
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kPeopleTable, 
			kPeopleLastName, 
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kPeopleTable, 
			kPeoplePasswordHash, 
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kPeopleTable,
			kPeopleEmailVerifiedAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kPeopleTable,
			kPeopleMustChangePassword,
			DB_TYPE_BOOL,
			kDatabaseInfoDefaultFalse);
        databaseInfo.AddColumnNotNullableWithDefault(
			kPeopleTable,
			kPeopleCreatedAt,
			DB_TYPE_BIGINT,
            kDatabaseInfoDefaultNow);
        databaseInfo.AddColumnNotNullableWithDefault(
            kPeopleTable,
            kPeopleUpdatedAt,
			DB_TYPE_BIGINT,
            kDatabaseInfoDefaultNow);
		// Lockout slots populated in Phase 5. failed_login_attempts is
		// monotonic until a successful login resets it; locked_until is the
		// epoch-microsecond timestamp past which the account is allowed to
		// log in again (NULL when the account is not locked). Phase 1.7 of
		// the security review.
		databaseInfo.AddColumnNotNullableWithDefault(
			kPeopleTable,
			kPeopleFailedLoginAttempts,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultZero);
		databaseInfo.AddColumnNullable(
			kPeopleTable,
			kPeopleLockedUntil,
			DB_TYPE_BIGINT);
	}

}  // namespace DbSchema