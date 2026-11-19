#pragma once

#include <string>

#include "transaction.h"
#include "sql_util/schema/database_info.h"

namespace DbMeta {

    // The startup schema check (CommunityFinder CM3 Phase 2 §8.2, OQ20): does
    // the database have every table and column this build's composed
    // DatabaseInfo expects? A database older than the build fails in ways
    // that don't say why: CM3's event pages returned 500s because the gallery
    // table wasn't there, while the event list, which never reads it, worked.
    // The check turns that into one warning at startup that names everything
    // missing.
    //
    // Only what's missing counts. Tables and columns the database has but the
    // build doesn't know about are ignored: an app's own extras, or a column a
    // newer build added before this one was rolled back.
    struct MissingSchema {
        // Tables the build expects and the database doesn't have, in the
        // build's order.
        StringArray tables;
        // "table.column", for columns missing from tables the database does
        // have. A missing table's columns aren't listed again.
        StringArray columns;

        bool Empty() const { return tables.empty() && columns.empty(); }
    };

    // Compares the build's tables and columns with information_schema's, in
    // the public schema. One query.
    MissingSchema FindMissingSchema(
        Transaction& transaction, const DbSchema::DatabaseInfo& databaseInfo);

    // The warning for `missing`, all on one line, or "" when nothing is
    // missing. For example:
    //   The database is older than this build: missing table
    //   event_gallery_photos; missing column events.video_url. Run the
    //   database helper with --migrate, or --recreate_database on a dev
    //   database.
    std::string DescribeMissingSchema(const MissingSchema& missing);

    // For an app's main(), once at startup, in its own transaction: logs the
    // warning when anything is missing, and returns whether it did. The
    // server keeps running either way, since most of it may still work. A
    // check that can't run (the query fails) logs that and returns false; it
    // never throws.
    bool WarnIfDatabaseIsOlderThanBuild(
        Transaction& transaction, const DbSchema::DatabaseInfo& databaseInfo);

}  // namespace DbMeta
