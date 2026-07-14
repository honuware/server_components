#include "device_tokens.h"

#include "people.h"

namespace DbSchema {

	void MakeDeviceTokensTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kDeviceTokensTable);
		databaseInfo.AddColumnPrimaryKey(
			kDeviceTokensTable,
			kDeviceTokensId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnNotNullableWithDefault(
			kDeviceTokensTable,
			kDeviceTokensUuid,
			DB_TYPE_UUID,
			kDatabaseInfoDefaultGenRandomUuid);
		databaseInfo.AddColumnForeignKeyRef(
			kPeopleTable,
			kPeopleId,
			kDeviceTokensTable,
			kDeviceTokensPersonId);
		databaseInfo.AddColumnUnique(
			kDeviceTokensTable,
			kDeviceTokensSecretHash,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNotNullableWithDefault(
			kDeviceTokensTable,
			kDeviceTokensCreatedAt,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNullable(
			kDeviceTokensTable,
			kDeviceTokensLastUsedAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnSimple(
			kDeviceTokensTable,
			kDeviceTokensExpiresAt,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kDeviceTokensTable,
			kDeviceTokensRevoked,
			DB_TYPE_BOOL,
			kDatabaseInfoDefaultFalse);
		// The remember-me cookie carries `<tokenId>.<secret>` where tokenId is
		// the uuid column. The cookie path looks the row up by uuid, so we
		// need both correctness (no two tokens can collide on the same uuid)
		// and lookup performance. Phase 1.2 of the security review.
		databaseInfo.AddUniqueConstraint(kDeviceTokensTable, kDeviceTokensUuid);
	}

}  // namespace DbSchema
