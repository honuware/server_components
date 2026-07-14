#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// Phase 3.8 of the security review.
//
// A ColumnRedactionSet is the in-memory representation of the
// admin_column_redactions DB table — a sorted set of (table, column)
// pairs. It is the canonical object the JSON-boundary enforcement
// layer (DatabaseRESTHelper) consults to decide which columns to
// strip from generic CRUD responses.
//
// Production: WebApp loads it once at startup via LoadColumnRedactionSet.
// Tests: built either by querying the populated table or by hand
// (see admin_column_redactions_test.cpp).
using ColumnRedactionSet = std::set<std::pair<std::string, std::string>>;

class AdminColumnRedactions {
public:
    AdminColumnRedactions(DatabaseHelper databaseHelper);
    AdminColumnRedactions(const AdminColumnRedactions&) = default;
    AdminColumnRedactions& operator=(const AdminColumnRedactions&) = default;
    ~AdminColumnRedactions() = default;

    void AddAdminColumnRedaction(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName);

    void DeleteAdminColumnRedaction(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName);

    bool IsRedacted(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName);

    // Full table scan — used at startup to materialise the in-memory
    // ColumnRedactionSet. The result is cached on WebApp; do not call
    // this on every request.
    KeyValueTableArray GetAdminColumnRedactions(Transaction& transaction);

    // Convenience wrapper that converts the table contents into the
    // ColumnRedactionSet representation used by the JSON-boundary
    // enforcement layer.
    ColumnRedactionSet LoadColumnRedactionSet(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
