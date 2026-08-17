#include "site_assets.h"

namespace DbSchema {

	void MakeSiteAssetsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSiteAssets);
		databaseInfo.AddColumnPrimaryKey(
			kSiteAssets,
			kSiteAssetId,
			DB_TYPE_BIGSERIAL);
		// A slot value refers to an asset BY NAME, so the name has to identify
		// exactly one row — the same reason site_font_sources.source_key is unique.
		databaseInfo.AddColumnUnique(
			kSiteAssets,
			kSiteAssetName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteAssets,
			kSiteAssetType,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteAssets,
			kSiteAssetBytes,
			DB_TYPE_BYTES);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteAssets,
			kSiteAssetCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteAssets,
			kSiteAssetLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

}  // namespace DbSchema
