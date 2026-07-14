#include "idempotency_keys.h"

namespace DbSchema {

	void MakeIdempotencyKeysTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kIdempotencyKeysTable);
		databaseInfo.AddColumnPrimaryKey(
			kIdempotencyKeysTable,
			kIdempotencyKeysId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnSimple(
			kIdempotencyKeysTable,
			kIdempotencyKeysScope,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kIdempotencyKeysTable,
			kIdempotencyKeysKey,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kIdempotencyKeysTable,
			kIdempotencyKeysEndpoint,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kIdempotencyKeysTable,
			kIdempotencyKeysRequestHash,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kIdempotencyKeysTable,
			kIdempotencyKeysStatus,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kIdempotencyKeysTable,
			kIdempotencyKeysResponseJson,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNotNullableWithDefault(
			kIdempotencyKeysTable,
			kIdempotencyKeysCreatedUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnSimple(
			kIdempotencyKeysTable,
			kIdempotencyKeysExpiresUs,
			DB_TYPE_BIGINT);
		databaseInfo.AddUniqueConstraint(
			kIdempotencyKeysTable,
			kIdempotencyKeysScope,
			kIdempotencyKeysKey,
			kIdempotencyKeysEndpoint);
	}

}  // namespace DbSchema
