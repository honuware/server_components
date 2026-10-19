#include "password_reset_tokens.h"

#include "people.h"

namespace DbSchema {

	void MakePasswordResetTokensTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kPasswordResetTokensTable);
		databaseInfo.AddColumnPrimaryKey(
			kPasswordResetTokensTable,
			kPasswordResetTokensId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kPeopleTable,
			kPeopleId,
			kPasswordResetTokensTable,
			kPasswordResetTokensPersonId);
		databaseInfo.AddColumnSimple(
			kPasswordResetTokensTable,
			kPasswordResetTokensTokenHash,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kPasswordResetTokensTable,
			kPasswordResetTokensExpiresAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kPasswordResetTokensTable,
			kPasswordResetTokensAttempts,
			DB_TYPE_INT,
			kDatabaseInfoDefaultZero);
		databaseInfo.AddColumnNotNullableWithDefault(
			kPasswordResetTokensTable,
			kPasswordResetTokensCreatedAt,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		// One pending link per person: a new request replaces the old row, so
		// the old link stops working. The table helper deletes before it
		// inserts; this constraint catches any caller that forgets.
		databaseInfo.AddUniqueConstraint(
			kPasswordResetTokensTable, kPasswordResetTokensPersonId);
	}

}  // namespace DbSchema
