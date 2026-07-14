#include "admin_enum_values.h"

#include "db_schema/admin_enum_values.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

AdminEnumValues::AdminEnumValues(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

void AdminEnumValues::AddAdminEnumValue(
    Transaction& transaction,
    int64_t adminEnumId,
    std::string_view name,
    int value) {
    KeyValueTable keyValueTable = {
        { std::string(DbSchema::kAdminEnumValuesAdminEnumId), StringFromInt(adminEnumId) },
        { std::string(DbSchema::kAdminEnumValuesName), std::string(name) },
        { std::string(DbSchema::kAdminEnumValuesValue), std::to_string(value) }
    };
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumValuesTable,
        keyValueTable);
}

KeyValueTableArray AdminEnumValues::GetAdminEnumValuesByEnumId(
    Transaction& transaction,
    int64_t adminEnumId) {
    KeyValueTable filter = {
        { std::string(DbSchema::kAdminEnumValuesAdminEnumId), StringFromInt(adminEnumId) }
    };
    return DbCrud::GetRowsByValuesWithOrderBy(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumValuesTable,
        filter,
        DbSchema::kAdminEnumValuesValue,
        true);
}

void AdminEnumValues::UpdateAdminEnumValue(
    Transaction& transaction,
    int64_t id,
    std::string_view name,
    int value) {
    KeyValueTable keyValueTable = {
        { std::string(DbSchema::kAdminEnumValuesName), std::string(name) },
        { std::string(DbSchema::kAdminEnumValuesValue), std::to_string(value) }
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumValuesTable,
        DbSchema::kAdminEnumValuesId,
        StringFromInt(id),
        keyValueTable);
}

void AdminEnumValues::DeleteAdminEnumValue(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumValuesTable,
        DbSchema::kAdminEnumValuesId,
        StringFromInt(id));
}

}  // namespace TableHelpers
