#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Tenant Theming Phase 9 — images a theme carries with it.
	//
	// The URL slots (site_logo_url, site_favicon_url, site_hero_image_url) hold a
	// URL, which was fine while Phase 5 was "imagery v1, URL-based": a studio
	// pasted a link. A theme BUNDLE has to carry the image itself, or a theme
	// file is not portable and the fake-studio proof cannot be reproduced from
	// one — so those bytes need somewhere to live and a route to be served from.
	//
	// Deliberately NOT the photo_instances / source_photos machinery: that exists
	// to scale and cache derivatives of user-uploaded photos attached to a table
	// ROW. A logo or favicon is one file, served as-is, addressed by name and
	// belonging to no row. This mirrors site_font_faces, which made the same
	// choice for the same reason.
	inline constexpr std::string_view kSiteAssets = "site_assets";
	inline constexpr std::string_view kSiteAssetId = "site_asset_id";
	// The bundle filename, e.g. "logo.svg". Unique: it is how a slot value
	// refers to the asset, so two rows sharing one would be ambiguous.
	inline constexpr std::string_view kSiteAssetName = "name";
	// Image subtype — "png", "jpeg", "svg". Decided by the magic bytes on
	// import, never by the filename.
	inline constexpr std::string_view kSiteAssetType = "type";
	inline constexpr std::string_view kSiteAssetBytes = "bytes";
	inline constexpr std::string_view kSiteAssetCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kSiteAssetLastUpdatedAtUs = "last_updated_at_us";

	void MakeSiteAssetsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
