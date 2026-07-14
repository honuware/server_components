#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// photo_instances - stores actual photo binary data
	inline constexpr std::string_view kPhotoInstances = "photo_instances";
	inline constexpr std::string_view kPhotoInstanceId = "photo_instance_id";
	inline constexpr std::string_view kPhotoInstancePhoto = "photo";
	inline constexpr std::string_view kPhotoInstanceType = "type";
	inline constexpr std::string_view kPhotoInstanceWidth = "width";
	inline constexpr std::string_view kPhotoInstanceHeight = "height";
	inline constexpr std::string_view kPhotoInstanceCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kPhotoInstanceLastUpdatedAtUs = "last_updated_at_us";

	void MakePhotoInstancesTable(DatabaseInfo databaseInfo);

	// source_photos - original uploaded photos
	inline constexpr std::string_view kSourcePhotos = "source_photos";
	inline constexpr std::string_view kSourcePhotoId = "source_photo_id";
	inline constexpr std::string_view kSourcePhotoPhotoInstanceId = "photo_instance_id";
	inline constexpr std::string_view kSourcePhotoCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kSourcePhotoLastUpdatedAtUs = "last_updated_at_us";

	void MakeSourcePhotosTable(DatabaseInfo databaseInfo);

	// scaled_photos - cached resized versions of source photos
	inline constexpr std::string_view kScaledPhotos = "scaled_photos";
	inline constexpr std::string_view kScaledPhotoId = "scaled_photo_id";
	inline constexpr std::string_view kScaledPhotoSourcePhotoId = "source_photo_id";
	inline constexpr std::string_view kScaledPhotoPhotoInstanceId = "photo_instance_id";
	inline constexpr std::string_view kScaledPhotoWidth = "width";
	inline constexpr std::string_view kScaledPhotoHeight = "height";
	inline constexpr std::string_view kScaledPhotoCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kScaledPhotoLastUsedAtUs = "last_used_at_us";

	void MakeScaledPhotosTable(DatabaseInfo databaseInfo);

	// photo_support_tables - lists tables that support photo associations
	inline constexpr std::string_view kPhotoSupportTables = "photo_support_tables";
	inline constexpr std::string_view kPhotoSupportTablesTableName = "table_name";

	void MakePhotoSupportTablesTable(DatabaseInfo databaseInfo);

	// table_item_photos - links rows in tables to photos
	inline constexpr std::string_view kTableItemPhotos = "table_item_photos";
	inline constexpr std::string_view kTableItemPhotosId = "id";
	inline constexpr std::string_view kTableItemPhotosTableName = "table_name";
	inline constexpr std::string_view kTableItemPhotosTableItemId = "table_item_id";
	inline constexpr std::string_view kTableItemPhotosSourcePhotoId = "source_photo_id";

	void MakeTableItemPhotosTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema