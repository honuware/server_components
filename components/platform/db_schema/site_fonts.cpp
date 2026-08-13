#include "site_fonts.h"

namespace DbSchema {

	void MakeSiteFontSourcesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSiteFontSources);
		databaseInfo.AddColumnPrimaryKey(
			kSiteFontSources,
			kSiteFontSourceId,
			DB_TYPE_BIGSERIAL);
		// One row per handle — a font row points at a source by key, so two
		// sources sharing one would make the reference ambiguous.
		databaseInfo.AddColumnUnique(
			kSiteFontSources,
			kSiteFontSourceKey,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteFontSources,
			kSiteFontSourceDisplayName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteFontSources,
			kSiteFontSourceBaseUrl,
			DB_TYPE_STRING);
		// Optional — a source whose URLs need no extra parameters leaves it blank.
		databaseInfo.AddColumnNullable(
			kSiteFontSources,
			kSiteFontSourceQuerySuffix,
			DB_TYPE_STRING);
		// Optional — a source that needs no preconnect hint leaves it blank.
		databaseInfo.AddColumnNullable(
			kSiteFontSources,
			kSiteFontSourcePreconnectLines,
			DB_TYPE_STRING);
		// Lets a studio retire a source without deleting it and its fonts.
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontSources,
			kSiteFontSourceActive,
			DB_TYPE_BOOL,
			kDatabaseInfoDefaultTrue);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontSources,
			kSiteFontSourceCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontSources,
			kSiteFontSourceLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

	void MakeSiteFontsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSiteFonts);
		databaseInfo.AddColumnPrimaryKey(
			kSiteFonts,
			kSiteFontId,
			DB_TYPE_BIGSERIAL);
		// D13: both required, neither ever defaulted in code.
		databaseInfo.AddColumnSimple(
			kSiteFonts,
			kSiteFontFamily,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteFonts,
			kSiteFontFallback,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteFonts,
			kSiteFontSourceKind,
			DB_TYPE_STRING);
		// Set for `cdn` rows only, so it is nullable rather than a hard FK — an
		// `uploaded` or `system` family has no source at all.
		databaseInfo.AddColumnNullable(
			kSiteFonts,
			kSiteFontFontSourceId,
			DB_TYPE_BIGINT);
		databaseInfo.AddColumnNullable(
			kSiteFonts,
			kSiteFontSpec,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFonts,
			kSiteFontOrdinal,
			DB_TYPE_INT,
			"0");
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFonts,
			kSiteFontActive,
			DB_TYPE_BOOL,
			kDatabaseInfoDefaultTrue);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFonts,
			kSiteFontCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFonts,
			kSiteFontLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

	void MakeSiteFontFacesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kSiteFontFaces);
		databaseInfo.AddColumnPrimaryKey(
			kSiteFontFaces,
			kSiteFontFaceId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kSiteFonts, kSiteFontId, kSiteFontFaces, kSiteFontFaceFontId);
		// CSS numeric weight (400, 700). Stored as an int so the @font-face rule
		// and the picker both get a real number rather than "bold"-ish prose.
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontFaces,
			kSiteFontFaceWeight,
			DB_TYPE_INT,
			"400");
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontFaces,
			kSiteFontFaceStyle,
			DB_TYPE_STRING,
			"'normal'");
		// Decided by the magic bytes on upload, never by the filename.
		databaseInfo.AddColumnSimple(
			kSiteFontFaces,
			kSiteFontFaceFormat,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kSiteFontFaces,
			kSiteFontFaceBytes,
			DB_TYPE_BYTES);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontFaces,
			kSiteFontFaceCreatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
		databaseInfo.AddColumnNotNullableWithDefault(
			kSiteFontFaces,
			kSiteFontFaceLastUpdatedAtUs,
			DB_TYPE_BIGINT,
			kDatabaseInfoDefaultNow);
	}

}  // namespace DbSchema
