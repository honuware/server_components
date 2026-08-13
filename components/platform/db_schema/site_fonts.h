#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Tenant Theming Phase 4B (D12/D13/D14) — fonts as per-tenant DATA.
	//
	// Three tables. The shape is the Google Fonts API's own, because that is what
	// makes "add a font" a row rather than a code change:
	//
	//   site_font_sources  where families can be fetched from (base URL,
	//                      preconnect hosts, the query suffix)
	//   site_fonts         the tenant's family inventory (family + fallback +
	//                      how it is sourced)
	//   site_font_faces    the binary faces of an UPLOADED family
	//
	// D12: sources are tenant-editable rows, NOT a framework allow-list. Mason
	// chose the flexibility over the guardrail with the costs stated. What
	// survives is hygiene, not policy: URLs must be https and well-formed.

	// ---- site_font_sources ---------------------------------------------------
	inline constexpr std::string_view kSiteFontSources = "site_font_sources";
	inline constexpr std::string_view kSiteFontSourceId = "site_font_source_id";
	// Stable handle, e.g. "google". What a font row points at.
	inline constexpr std::string_view kSiteFontSourceKey = "source_key";
	inline constexpr std::string_view kSiteFontSourceDisplayName = "display_name";
	// e.g. https://fonts.googleapis.com/css2
	inline constexpr std::string_view kSiteFontSourceBaseUrl = "base_url";
	// Appended once to each constructed stylesheet URL, e.g. "display=swap".
	inline constexpr std::string_view kSiteFontSourceQuerySuffix = "query_suffix";
	// Newline-separated `href|crossorigin` lines — the site_social_links
	// precedent (OQ-T4) rather than a fourth table. `crossorigin` is "true"/"false".
	inline constexpr std::string_view kSiteFontSourcePreconnectLines =
		"preconnect_lines";
	inline constexpr std::string_view kSiteFontSourceActive = "active";
	inline constexpr std::string_view kSiteFontSourceCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kSiteFontSourceLastUpdatedAtUs =
		"last_updated_at_us";

	// ---- site_fonts ----------------------------------------------------------
	inline constexpr std::string_view kSiteFonts = "site_fonts";
	inline constexpr std::string_view kSiteFontId = "site_font_id";
	// D13: family and fallback are SEPARATE required columns, and no generic
	// family is ever defaulted in code. A role token names the family; the server
	// composes `'Family', fallback` from this row.
	inline constexpr std::string_view kSiteFontFamily = "family";
	inline constexpr std::string_view kSiteFontFallback = "fallback";
	inline constexpr std::string_view kSiteFontSourceKind = "source_kind";
	// Set for `cdn` rows only.
	inline constexpr std::string_view kSiteFontFontSourceId = "font_source_id";
	// The stylesheet query fragment for a `cdn` row, e.g.
	// "family=Barlow:wght@100..900". Fragments for one source are joined with &.
	inline constexpr std::string_view kSiteFontSpec = "spec";
	inline constexpr std::string_view kSiteFontOrdinal = "ordinal";
	inline constexpr std::string_view kSiteFontActive = "active";
	inline constexpr std::string_view kSiteFontCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kSiteFontLastUpdatedAtUs = "last_updated_at_us";

	// D13's three kinds. `system` is what lets "no assumed sans-serif" work
	// without a special case: a studio wanting Georgia creates a real row with no
	// source, and its fallback is data like every other row's.
	inline constexpr std::string_view kSiteFontSourceKindCdn = "cdn";
	inline constexpr std::string_view kSiteFontSourceKindUploaded = "uploaded";
	inline constexpr std::string_view kSiteFontSourceKindSystem = "system";

	// ---- site_font_faces -----------------------------------------------------
	inline constexpr std::string_view kSiteFontFaces = "site_font_faces";
	inline constexpr std::string_view kSiteFontFaceId = "site_font_face_id";
	inline constexpr std::string_view kSiteFontFaceFontId = "site_font_id";
	inline constexpr std::string_view kSiteFontFaceWeight = "weight";
	inline constexpr std::string_view kSiteFontFaceStyle = "style";
	// woff2 / woff / ttf / otf — decided by the magic bytes, not by the filename.
	inline constexpr std::string_view kSiteFontFaceFormat = "format";
	inline constexpr std::string_view kSiteFontFaceBytes = "bytes";
	inline constexpr std::string_view kSiteFontFaceCreatedAtUs = "created_at_us";
	inline constexpr std::string_view kSiteFontFaceLastUpdatedAtUs =
		"last_updated_at_us";

	void MakeSiteFontSourcesTable(DatabaseInfo databaseInfo);
	void MakeSiteFontsTable(DatabaseInfo databaseInfo);
	void MakeSiteFontFacesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
