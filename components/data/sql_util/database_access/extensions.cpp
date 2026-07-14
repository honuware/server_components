#include "extensions.h"

#include <array>

namespace Extensions {

namespace {

// Add the next Postgres extension here. Names must be valid identifiers per
// Postgres docs (no quoting needed). Keep alphabetized for readability.
constexpr std::array<std::string_view, 1> kExtensionNames = {
    "citext"
};

}  // namespace

void CreateExtensions(Transaction& transaction) {
    // One statement per call — RunSqlStatement uses pqxx exec_params0 which
    // only handles a single statement. Looping keeps every addition a
    // one-liner in kExtensionNames without worrying about multi-statement
    // SQL.
    for (std::string_view name : kExtensionNames) {
        std::string sql = "CREATE EXTENSION IF NOT EXISTS ";
        sql += name;
        transaction.RunSqlStatement(sql);
    }
}

std::string GenerateCreateExtensionsSql() {
    // The test bootstrap (GlobalDatabaseTestSupport::SetupAllTables) sends
    // its DDL through pqxx::work::exec which DOES support multi-statement
    // strings via the simple query protocol, so a single concatenated string
    // is fine here. Trailing newline lets callers append more DDL without
    // worrying about delimiters.
    std::string sql;
    for (std::string_view name : kExtensionNames) {
        sql += "CREATE EXTENSION IF NOT EXISTS ";
        sql += name;
        sql += ";\n";
    }
    return sql;
}

}  // namespace Extensions
