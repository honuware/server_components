#include "site_fonts.h"

#include "db_schema/site_fonts.h"
#include "sql_util/database_access/bytea.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

constexpr std::string_view kSqlAllSources =
    "SELECT * FROM site_font_sources ORDER BY source_key ASC";

constexpr std::string_view kSqlActiveSources =
    "SELECT * FROM site_font_sources WHERE active = true ORDER BY source_key ASC";

// Ordered so the constructed stylesheet URL is stable between requests — a
// churning URL would miss the CDN's cache on every page load.
constexpr std::string_view kSqlAllFonts =
    "SELECT * FROM site_fonts ORDER BY ordinal ASC, site_font_id ASC";

constexpr std::string_view kSqlActiveFonts =
    "SELECT * FROM site_fonts WHERE active = true "
    "ORDER BY ordinal ASC, site_font_id ASC";

// The face list deliberately does NOT select `bytes` — it feeds the site_info
// payload, which needs descriptors, not megabytes of font data.
constexpr std::string_view kSqlFacesForFont =
    "SELECT site_font_face_id, site_font_id, weight, style, format, "
    "created_at_us, last_updated_at_us FROM site_font_faces "
    "WHERE site_font_id = $1 ORDER BY weight ASC, style ASC";

// Every uploaded face joined to its family, so the payload builder needs one
// query rather than one per family.
constexpr std::string_view kSqlAllActiveFaces =
    "SELECT f.site_font_face_id, f.site_font_id, f.weight, f.style, f.format, "
    "s.family, s.fallback "
    "FROM site_font_faces f JOIN site_fonts s ON s.site_font_id = f.site_font_id "
    "WHERE s.active = true "
    "ORDER BY s.ordinal ASC, s.site_font_id ASC, f.weight ASC, f.style ASC";

}  // namespace

SiteFonts::SiteFonts(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

// ---- sources ---------------------------------------------------------------

int64_t SiteFonts::AddSource(
    Transaction& transaction,
    std::string_view sourceKey,
    std::string_view displayName,
    std::string_view baseUrl,
    std::string_view querySuffix,
    std::string_view preconnectLines) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSiteFontSourceKey), std::string(sourceKey) },
        { std::string(DbSchema::kSiteFontSourceDisplayName), std::string(displayName) },
        { std::string(DbSchema::kSiteFontSourceBaseUrl), std::string(baseUrl) },
        { std::string(DbSchema::kSiteFontSourceQuerySuffix), std::string(querySuffix) },
        { std::string(DbSchema::kSiteFontSourcePreconnectLines),
          std::string(preconnectLines) },
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kSiteFontSources, kv);
}

KeyValueTable SiteFonts::GetSource(Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFontSources, DbSchema::kSiteFontSourceId, StringFromInt(id));
}

KeyValueTable SiteFonts::GetSourceByKey(
    Transaction& transaction, std::string_view sourceKey) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFontSources, DbSchema::kSiteFontSourceKey, sourceKey);
}

KeyValueTableArray SiteFonts::GetAllSources(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlAllSources));
}

KeyValueTableArray SiteFonts::GetActiveSources(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlActiveSources));
}

void SiteFonts::DeleteSource(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFontSources, DbSchema::kSiteFontSourceId, StringFromInt(id));
}

// ---- families --------------------------------------------------------------

int64_t SiteFonts::AddFont(
    Transaction& transaction,
    std::string_view family,
    std::string_view fallback,
    std::string_view sourceKind,
    int64_t fontSourceId,
    std::string_view spec,
    int ordinal) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSiteFontFamily), std::string(family) },
        { std::string(DbSchema::kSiteFontFallback), std::string(fallback) },
        { std::string(DbSchema::kSiteFontSourceKind), std::string(sourceKind) },
        { std::string(DbSchema::kSiteFontSpec), std::string(spec) },
        { std::string(DbSchema::kSiteFontOrdinal), StringFromInt(ordinal) },
    };
    // Only a `cdn` family has a source; leaving the column out for the others
    // keeps "no source" as NULL rather than a dangling id 0.
    if (fontSourceId > 0) {
        kv[std::string(DbSchema::kSiteFontFontSourceId)] = StringFromInt(fontSourceId);
    }
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kSiteFonts, kv);
}

KeyValueTable SiteFonts::GetFont(Transaction& transaction, int64_t id) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFonts, DbSchema::kSiteFontId, StringFromInt(id));
}

KeyValueTable SiteFonts::GetFontByFamily(
    Transaction& transaction, std::string_view family) const {
    return DbCrud::GetRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFonts, DbSchema::kSiteFontFamily, family);
}

KeyValueTableArray SiteFonts::GetAllFonts(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlAllFonts));
}

KeyValueTableArray SiteFonts::GetActiveFonts(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlActiveFonts));
}

void SiteFonts::DeleteFont(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFonts, DbSchema::kSiteFontId, StringFromInt(id));
}

// ---- uploaded faces --------------------------------------------------------

int64_t SiteFonts::AddFace(
    Transaction& transaction,
    int64_t siteFontId,
    int weight,
    std::string_view style,
    std::string_view format,
    std::string_view bytes) {
    KeyValueTable kv = {
        { std::string(DbSchema::kSiteFontFaceFontId), StringFromInt(siteFontId) },
        { std::string(DbSchema::kSiteFontFaceWeight), StringFromInt(weight) },
        { std::string(DbSchema::kSiteFontFaceStyle), std::string(style) },
        { std::string(DbSchema::kSiteFontFaceFormat), std::string(format) },
        // Binary values cross the KeyValueTable layer as bytea hex text.
        { std::string(DbSchema::kSiteFontFaceBytes),
          SqlUtil::ByteaHexEncode(bytes) },
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, DbSchema::kSiteFontFaces, kv);
}

KeyValueTable SiteFonts::GetFace(Transaction& transaction, int64_t id) const {
    return transaction.RunSqlStatementReturningOneRow(
        "SELECT site_font_face_id, site_font_id, weight, style, format, "
        "created_at_us, last_updated_at_us FROM site_font_faces "
        "WHERE site_font_face_id = $1",
        StringFromInt(id));
}

std::string SiteFonts::GetFaceBytes(Transaction& transaction, int64_t id) const {
    return SqlUtil::ByteaHexDecode(transaction.RunSqlStatementReturningOneValue(
        "SELECT bytes FROM site_font_faces WHERE site_font_face_id = $1",
        StringFromInt(id)));
}

KeyValueTableArray SiteFonts::GetFacesForFont(
    Transaction& transaction, int64_t siteFontId) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlFacesForFont), StringFromInt(siteFontId));
}

KeyValueTableArray SiteFonts::GetAllActiveFaces(Transaction& transaction) const {
    return transaction.RunSqlStatementReturningKeyValueTableArray(
        std::string(kSqlAllActiveFaces));
}

void SiteFonts::DeleteFace(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kSiteFontFaces, DbSchema::kSiteFontFaceId, StringFromInt(id));
}

}  // namespace TableHelpers
