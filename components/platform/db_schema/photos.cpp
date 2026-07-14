#include "photos.h"

namespace DbSchema {

	void MakePhotoInstancesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kPhotoInstances);
		databaseInfo.AddColumnPrimaryKey(
			kPhotoInstances,
			kPhotoInstanceId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnSimple(
			kPhotoInstances,
			kPhotoInstancePhoto,
			DB_TYPE_BYTES);
		databaseInfo.AddColumnSimple(
			kPhotoInstances,
			kPhotoInstanceType,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kPhotoInstances,
			kPhotoInstanceWidth,
			DB_TYPE_INT);
		databaseInfo.AddColumnSimple(
			kPhotoInstances,
			kPhotoInstanceHeight,
			DB_TYPE_INT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kPhotoInstances,
			kPhotoInstanceCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kPhotoInstances,
			kPhotoInstanceLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

	void MakeSourcePhotosTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSourcePhotos);
		databaseInfo.AddColumnPrimaryKey(
			kSourcePhotos,
			kSourcePhotoId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kPhotoInstances, kPhotoInstanceId, kSourcePhotos, kSourcePhotoPhotoInstanceId);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSourcePhotos,
			kSourcePhotoCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSourcePhotos,
			kSourcePhotoLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

	void MakeScaledPhotosTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kScaledPhotos);
		databaseInfo.AddColumnPrimaryKey(
			kScaledPhotos,
			kScaledPhotoId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kSourcePhotos, kSourcePhotoId, kScaledPhotos, kScaledPhotoSourcePhotoId);
		databaseInfo.AddColumnForeignKeyRef(
			kPhotoInstances, kPhotoInstanceId, kScaledPhotos, kScaledPhotoPhotoInstanceId);
		databaseInfo.AddColumnSimple(
			kScaledPhotos,
			kScaledPhotoWidth,
			DB_TYPE_INT);
		databaseInfo.AddColumnSimple(
			kScaledPhotos,
			kScaledPhotoHeight,
			DB_TYPE_INT);
		databaseInfo.AddColumnNotNullableWithDefault(
			kScaledPhotos,
			kScaledPhotoCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kScaledPhotos,
			kScaledPhotoLastUsedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

	void MakePhotoSupportTablesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kPhotoSupportTables);
		databaseInfo.AddColumnPrimaryKey(
			kPhotoSupportTables,
			kPhotoSupportTablesTableName,
			DB_TYPE_STRING);
	}

	void MakeTableItemPhotosTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kTableItemPhotos);
		databaseInfo.AddColumnPrimaryKey(
			kTableItemPhotos,
			kTableItemPhotosId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kPhotoSupportTables, kPhotoSupportTablesTableName,
			kTableItemPhotos, kTableItemPhotosTableName);
		databaseInfo.AddColumnSimple(
			kTableItemPhotos,
			kTableItemPhotosTableItemId,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnForeignKeyRef(
			kSourcePhotos, kSourcePhotoId,
			kTableItemPhotos, kTableItemPhotosSourcePhotoId);
		databaseInfo.AddUniqueConstraint(
			kTableItemPhotos,
			kTableItemPhotosTableName,
			kTableItemPhotosTableItemId);
	}

}  // namespace DbSchema