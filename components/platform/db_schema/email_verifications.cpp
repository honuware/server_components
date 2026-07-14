#include "email_verifications.h"

#include "people.h"

namespace DbSchema {

	void MakeEmailVerificationsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kEmailVerificationsTable);
		databaseInfo.AddColumnPrimaryKey(
			kEmailVerificationsTable,
			kEmailVerificationsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kPeopleTable,
			kPeopleId,
			kEmailVerificationsTable,
            kEmailVerificationsPersonId);
        // These should be BASE64-encoded strings.
		databaseInfo.AddColumnSimple(
			kEmailVerificationsTable,
			kEmailVerificationsTokenHash,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kEmailVerificationsTable,
			kEmailVerificationsExpiresAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kEmailVerificationsTable,
			kEmailVerificationsAttempts,
			DB_TYPE_INT,
			kDatabaseInfoDefaultZero);
		// Each person should have at most one pending verification at any
		// time. The table helper enforces this by deleting any existing row
		// before insert; the DB constraint catches any future caller that
		// forgets. Phase 1.3 of the security review.
		databaseInfo.AddUniqueConstraint(
			kEmailVerificationsTable, kEmailVerificationsPersonId);
	}

}  // namespace DbSchema