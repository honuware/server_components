#include "admin_column_enums.h"

#include "db_schema/admin_column_enums.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/types.h"

namespace TableHelpers {

AdminColumnEnums::AdminColumnEnums(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {
}

void AdminColumnEnums::AddAdminColumnEnum(
    Transaction& transaction,
    int64_t adminEnumId,
    int64_t adminColumnDataInfoId) {
    KeyValueTable keyValueTable = {
        { std::string(DbSchema::kAdminColumnEnumsAdminEnumId), StringFromInt(adminEnumId) },
        { std::string(DbSchema::kAdminColumnEnumsAdminColumnDataInfoId), StringFromInt(adminColumnDataInfoId) }
    };
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnEnumsTable,
        keyValueTable);
}

bool AdminColumnEnums::GetAdminColumnEnumByColumnDataInfoId(
    Transaction& transaction,
    int64_t columnDataInfoId,
    KeyValueTable& keyValueTable) {
    keyValueTable = DbCrud::LookupRowByValue(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnEnumsTable,
        DbSchema::kAdminColumnEnumsAdminColumnDataInfoId,
        StringFromInt(columnDataInfoId));
    return !keyValueTable.empty();
}

KeyValueTableArray AdminColumnEnums::GetAdminColumnEnums(Transaction& transaction) {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kAdminColumnEnumsTable);
}

void AdminColumnEnums::DeleteAdminColumnEnum(Transaction& transaction, int64_t id) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminColumnEnumsTable,
        DbSchema::kAdminColumnEnumsId,
        StringFromInt(id));
}

}  // namespace TableHelpers
