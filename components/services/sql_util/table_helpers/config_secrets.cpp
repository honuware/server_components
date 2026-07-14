#include "sql_util/table_helpers/config_secrets.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "db_schema/config_secrets.h"

namespace TableHelpers {

ConfigSecrets::ConfigSecrets(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

void ConfigSecrets::AddSecret(
    Transaction& transaction, 
    std::string_view name, 
    std::string_view value) {
    DbCrud::DeleteRow(
        transaction, databaseHelper_, DbSchema::kConfigSecretsTable,
        DbSchema::kConfigSecretsName, name);
    KeyValueTable row = {
        {std::string(DbSchema::kConfigSecretsName), std::string(name)},
        {std::string(DbSchema::kConfigSecretsValue), std::string(value)}
    };
    DbCrud::AddRowToTable(
        transaction, databaseHelper_, DbSchema::kConfigSecretsTable, row);
}

std::string ConfigSecrets::LookupSecret(
    Transaction& transaction, std::string_view name) {
    KeyValueTable row = DbCrud::LookupRowByValue(
        transaction, databaseHelper_, DbSchema::kConfigSecretsTable, 
        DbSchema::kConfigSecretsName, name);
    if (!row.empty()) {
        return row.at(std::string(DbSchema::kConfigSecretsValue));
    }
    return std::string();
}

} // namespace TableHelpers