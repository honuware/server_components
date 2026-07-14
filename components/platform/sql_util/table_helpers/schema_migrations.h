#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class SchemaMigrations {
public:
    explicit SchemaMigrations(DatabaseHelper databaseHelper);
    SchemaMigrations(const SchemaMigrations&) = default;
    SchemaMigrations& operator=(const SchemaMigrations&) = default;
    ~SchemaMigrations() = default;

    // Returns true if `id` is recorded as applied in schema_migrations.
    bool IsApplied(Transaction& transaction, std::string_view id) const;

    // Returns all applied ids in chronological order
    // (applied_at_us ASC, id ASC as a deterministic tiebreak when two
    // migrations land in the same microsecond — unlikely in production,
    // possible in tests that share a transaction).
    StringArray ListAppliedIds(Transaction& transaction) const;

    // Inserts (id, now_us()) into schema_migrations. Caller is responsible
    // for ensuring the id is not already present; a duplicate primary key
    // will raise from the underlying SQL layer.
    void RecordApplied(Transaction& transaction, std::string_view id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
