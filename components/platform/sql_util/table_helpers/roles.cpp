#include "roles.h"

#include <stdexcept>

#include "db_schema/roles.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

Roles::Roles(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t Roles::AddRole(
    Transaction& transaction,
    std::string_view name,
    std::string_view description) {
    KeyValueTable kv = {
        { std::string(DbSchema::kRolesName), std::string(name) },
        { std::string(DbSchema::kRolesDescription), std::string(description) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        kv);
}

KeyValueTable Roles::GetRole(Transaction& transaction, int64_t id) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        DbSchema::kRolesId,
        StringFromInt(id));
    if (row.empty()) {
        throw std::runtime_error("Roles::GetRole(id) - not found");
    }
    return row;
}

KeyValueTable Roles::GetRole(Transaction& transaction, std::string_view name) const {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        DbSchema::kRolesName,
        name);
    if (row.empty()) {
        throw std::runtime_error("Roles::GetRole(name) - not found");
    }
    return row;
}

KeyValueTableArray Roles::GetRoles(Transaction& transaction) const {
    return DbCrud::GetTableRows(transaction, databaseHelper_, DbSchema::kRolesTable);
}

void Roles::SetName(Transaction& transaction, int64_t id, std::string_view name) {
    // Ensure exists
    (void)GetRole(transaction, id);

    KeyValueTable kv = {
        { std::string(DbSchema::kRolesName), std::string(name)}
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        DbSchema::kRolesId,
        StringFromInt(id),
        kv);
}

void Roles::SetDescription(Transaction& transaction, int64_t id, std::string_view description) {
    // Ensure exists
    (void)GetRole(transaction, id);

    KeyValueTable kv = {
        { std::string(DbSchema::kRolesDescription), std::string(description) }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        DbSchema::kRolesId,
        StringFromInt(id),
        kv);
}

void Roles::DeleteRole(Transaction& transaction, int64_t id) {
    // This delete should be a no-op if nothing matches; do not throw on not-found.
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kRolesTable,
        DbSchema::kRolesId,
        StringFromInt(id));
}

} // namespace TableHelpers