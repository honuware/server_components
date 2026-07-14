#include "role_assignments.h"

#include <stdexcept>

#include "db_schema/role_assignments.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

RoleAssignments::RoleAssignments(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t RoleAssignments::AddRoleAssignment(
    Transaction& transaction,
    int64_t personId,
    int64_t roleId) {
    KeyValueTable kv = {
        { std::string(DbSchema::kRoleAssignmentsPersonId), StringFromInt(personId) },
        { std::string(DbSchema::kRoleAssignmentsRoleId), StringFromInt(roleId) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kRoleAssignmentsTable,
        kv);
}

KeyValueTable RoleAssignments::GetRoleAssignments(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kRoleAssignmentsTable,
        DbSchema::kRoleAssignmentsId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("RoleAssignments::GetRoleAssignments - not found");
    }
    return row;
}

KeyValueTableArray RoleAssignments::GetRoleAssignmentsForPerson(Transaction& transaction, int64_t personId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kRoleAssignmentsPersonId), StringFromInt(personId) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kRoleAssignmentsTable,
        filter,
        DbSchema::kRoleAssignmentsId,
        true);
}

KeyValueTableArray RoleAssignments::GetRoleAssignmentsForRole(Transaction& transaction, int64_t roleId) const {
    KeyValueTable filter = {
        { std::string(DbSchema::kRoleAssignmentsRoleId), StringFromInt(roleId) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kRoleAssignmentsTable,
        filter,
        DbSchema::kRoleAssignmentsId,
        true);
}

KeyValueTableArray RoleAssignments::GetRoleAssignments(Transaction& transaction) const {
    return DbCrud::GetTableRows(transaction, databaseHelper_, DbSchema::kRoleAssignmentsTable);
}

void RoleAssignments::DeleteRoleAssignment(Transaction& transaction, int64_t id) {
    // No-op delete if not found; should not throw.
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kRoleAssignmentsTable,
        DbSchema::kRoleAssignmentsId,
        StringFromInt(id));
}

} // namespace TableHelpers