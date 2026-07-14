#include "permission_implications.h"

#include <string>

#include "db_schema/permission_implications.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

PermissionImplications::PermissionImplications(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t PermissionImplications::AddImplication(
    Transaction& transaction,
    int64_t permissionId,
    int64_t impliesPermissionId) {
    KeyValueTable values = {
        { std::string(DbSchema::kPermissionImplicationsPermissionId),
          StringFromInt(permissionId) },
        { std::string(DbSchema::kPermissionImplicationsImpliesPermissionId),
          StringFromInt(impliesPermissionId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_,
        DbSchema::kPermissionImplicationsTable, values);
}

KeyValueTableArray PermissionImplications::GetDirectImplications(
    Transaction& transaction, int64_t permissionId) const {
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction, databaseHelper_,
        DbSchema::kPermissionImplicationsTable,
        { { std::string(DbSchema::kPermissionImplicationsPermissionId),
            StringFromInt(permissionId) } },
        DbSchema::kPermissionImplicationsImpliesPermissionId, true);
}

KeyValueTableArray PermissionImplications::GetAll(Transaction& transaction) const {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kPermissionImplicationsTable);
}

void PermissionImplications::DeleteImplication(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_,
        DbSchema::kPermissionImplicationsTable,
        DbSchema::kPermissionImplicationsId, StringFromInt(id));
}

std::set<int64_t> PermissionImplications::ExpandClosure(
    Transaction& transaction,
    const std::vector<int64_t>& seedPermissionIds) const {
    std::set<int64_t> result(seedPermissionIds.begin(), seedPermissionIds.end());
    if (seedPermissionIds.empty()) {
        return result;
    }

    // Seeds are int64 ids resolved server-side (not user text), so inlining
    // them in a VALUES list is safe. One recursive CTE walks the DAG.
    std::string values;
    for (size_t i = 0; i < seedPermissionIds.size(); ++i) {
        if (i != 0) values += ",";
        // Cast to bigint so the seed column type matches implies_permission_id
        // (the recursive CTE requires union-compatible column types).
        values += "(" + std::to_string(seedPermissionIds[i]) + "::bigint)";
    }
    std::string sql =
        "WITH RECURSIVE seed(permission_id) AS (VALUES " + values + "), "
        "closure AS ("
        "  SELECT permission_id FROM seed "
        "  UNION "
        "  SELECT pi.implies_permission_id "
        "  FROM permission_implications pi "
        "  JOIN closure c ON pi.permission_id = c.permission_id"
        ") SELECT DISTINCT permission_id FROM closure";

    auto rows = transaction.RunSqlStatementReturningKeyValueTableArray(sql);
    for (const auto& row : rows) {
        auto it = row.find(std::string(DbSchema::kPermissionImplicationsPermissionId));
        if (it != row.end() && !it->second.empty()) {
            result.insert(std::stoll(it->second));
        }
    }
    return result;
}

}  // namespace TableHelpers
