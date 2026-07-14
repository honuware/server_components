#include "admin_enums.h"

#include "db_schema/admin_enums.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

AdminEnums::AdminEnums(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

int64_t AdminEnums::AddAdminEnum(
    Transaction& transaction,
    std::string_view name) {
    KeyValueTable keyValueTable = {
        { std::string(DbSchema::kAdminEnumsName), std::string(name) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumsTable,
        keyValueTable);
}

bool AdminEnums::GetAdminEnum(
    Transaction& transaction,
    std::string_view name,
    KeyValueTable& keyValueTable) {
    keyValueTable = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumsTable,
        DbSchema::kAdminEnumsName,
        name);
    return !keyValueTable.empty();
}

KeyValueTableArray AdminEnums::GetAdminEnums(Transaction& transaction) {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kAdminEnumsTable);
}

void AdminEnums::DeleteAdminEnum(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminEnumsTable,
        DbSchema::kAdminEnumsId,
        StringFromInt(id));
}

}  // namespace TableHelpers
