#include "sql_util/database_access/schema_check.h"

#include <exception>
#include <map>
#include <set>
#include <sstream>

#include "util/logging.h"

namespace DbMeta {

    namespace {

        // Every column of every table in the public schema, in one read.
        constexpr std::string_view kListPublicColumnsSql = R"SQL(
SELECT table_name, column_name
FROM information_schema.columns
WHERE table_schema = 'public'
)SQL";

    }  // namespace

    MissingSchema FindMissingSchema(
        Transaction& transaction, const DbSchema::DatabaseInfo& databaseInfo) {
        std::map<std::string, std::set<std::string>> databaseColumns;
        for (const KeyValueTable& row :
                transaction.RunSqlStatementReturningKeyValueTableArray(
                    kListPublicColumnsSql)) {
            databaseColumns[row.at("table_name")].insert(row.at("column_name"));
        }

        MissingSchema missing;
        for (const std::string& tableName : databaseInfo.GetAllTables()) {
            auto found = databaseColumns.find(tableName);
            if (found == databaseColumns.end()) {
                missing.tables.push_back(tableName);
                continue;
            }
            for (const DbSchema::ColumnInfo& column :
                    databaseInfo.GetTableInfo(tableName).GetColumns()) {
                if (found->second.count(column.GetColumnName()) == 0) {
                    missing.columns.push_back(tableName + "." + column.GetColumnName());
                }
            }
        }
        return missing;
    }

    std::string DescribeMissingSchema(const MissingSchema& missing) {
        if (missing.Empty()) {
            return "";
        }
        std::ostringstream message;
        message << "The database is older than this build: ";
        const char* separator = "";
        for (const std::string& table : missing.tables) {
            message << separator << "missing table " << table;
            separator = "; ";
        }
        for (const std::string& column : missing.columns) {
            message << separator << "missing column " << column;
            separator = "; ";
        }
        message << ". Run the database helper with --migrate, or "
                   "--recreate_database on a dev database.";
        return message.str();
    }

    bool WarnIfDatabaseIsOlderThanBuild(
        Transaction& transaction, const DbSchema::DatabaseInfo& databaseInfo) {
        try {
            const std::string warning =
                DescribeMissingSchema(FindMissingSchema(transaction, databaseInfo));
            if (warning.empty()) {
                return false;
            }
            LogWarning() << warning << "\n";
            return true;
        } catch (const std::exception& error) {
            LogWarning() << "Couldn't check the database against this build's schema: "
                         << error.what() << "\n";
            return false;
        }
    }

}  // namespace DbMeta
