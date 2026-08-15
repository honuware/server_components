#pragma once

#include <cstdint>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// CRUD for the per-tenant font inventory (Tenant Theming Phase 4B). The three
// tables are one cohesive unit — a source is only meaningful through the fonts
// that reference it, and a face only through its family — so they share a
// helper rather than being split three ways.
//
// See db_schema/site_fonts.h for what each row means.
class SiteFonts {
public:
    SiteFonts(DatabaseHelper databaseHelper);
    SiteFonts(const SiteFonts&) = default;
    SiteFonts& operator=(const SiteFonts&) = default;
    ~SiteFonts() = default;

    // ---- sources ----
    int64_t AddSource(
        Transaction& transaction,
        std::string_view sourceKey,
        std::string_view displayName,
        std::string_view baseUrl,
        std::string_view querySuffix,
        std::string_view preconnectLines);

    // Edit a source in place. The editor saves the whole inventory at once, and
    // rebuilding rows instead of updating them would hand every family a new
    // source id on each save.
    void UpdateSource(
        Transaction& transaction,
        int64_t id,
        std::string_view sourceKey,
        std::string_view displayName,
        std::string_view baseUrl,
        std::string_view querySuffix,
        std::string_view preconnectLines);

    KeyValueTable GetSource(Transaction& transaction, int64_t id) const;
    KeyValueTable GetSourceByKey(
        Transaction& transaction, std::string_view sourceKey) const;
    KeyValueTableArray GetAllSources(Transaction& transaction) const;
    KeyValueTableArray GetActiveSources(Transaction& transaction) const;
    void DeleteSource(Transaction& transaction, int64_t id);

    // ---- families ----
    // `fontSourceId` and `spec` are only meaningful for a `cdn` row; pass 0 and
    // "" for `uploaded` and `system` families.
    int64_t AddFont(
        Transaction& transaction,
        std::string_view family,
        std::string_view fallback,
        std::string_view sourceKind,
        int64_t fontSourceId,
        std::string_view spec,
        int ordinal);

    // Edit a family in place, KEEPING its id — which is what keeps its uploaded
    // faces attached. A save that deleted and re-added families would silently
    // destroy every font file the studio uploaded.
    void UpdateFont(
        Transaction& transaction,
        int64_t id,
        std::string_view family,
        std::string_view fallback,
        std::string_view sourceKind,
        int64_t fontSourceId,
        std::string_view spec,
        int ordinal);

    KeyValueTable GetFont(Transaction& transaction, int64_t id) const;
    KeyValueTable GetFontByFamily(
        Transaction& transaction, std::string_view family) const;
    KeyValueTableArray GetAllFonts(Transaction& transaction) const;
    KeyValueTableArray GetActiveFonts(Transaction& transaction) const;
    void DeleteFont(Transaction& transaction, int64_t id);

    // ---- uploaded faces ----
    int64_t AddFace(
        Transaction& transaction,
        int64_t siteFontId,
        int weight,
        std::string_view style,
        std::string_view format,
        // Raw font bytes. `std::string` rather than a Blob type because that is
        // how this codebase already moves binary column values (see
        // PhotoInstances) — the KeyValueTable layer carries them as strings.
        std::string_view bytes);

    KeyValueTable GetFace(Transaction& transaction, int64_t id) const;
    // Fetched on its own: the list reads above must not drag every font file
    // into memory just to render a manage page.
    std::string GetFaceBytes(Transaction& transaction, int64_t id) const;
    KeyValueTableArray GetFacesForFont(
        Transaction& transaction, int64_t siteFontId) const;
    // Every uploaded face for the tenant, joined to its family — what the
    // site_info payload needs to emit @font-face descriptors in one query.
    KeyValueTableArray GetAllActiveFaces(Transaction& transaction) const;
    void DeleteFace(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
