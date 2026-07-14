#include "sql_util/table_helpers/admin_table_display_template.h"

#include "sql_util/database_access/database_crud_helpers.h"
#include "db_schema/admin_table_display_template.h"

namespace TableHelpers {

AdminTableDisplayTemplate::AdminTableDisplayTemplate(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper)
{
}

std::string AdminTableDisplayTemplate::GetDisplayTemplate(Transaction& transaction, std::string_view tableName)
{
    KeyValueTable lookupValues;
    lookupValues[std::string(DbSchema::kAdminTableDisplayTemplateName)] = std::string(tableName);
    KeyValueTable result = DbCrud::LookupRowByValues(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableDisplayTemplateTable,
        lookupValues);
    auto it = result.find(std::string(DbSchema::kAdminTableDisplayTemplateTemplate));
    if (it != result.end()) {
        return it->second;
    }
    return "";
}

void AdminTableDisplayTemplate::SetDisplayTemplate(Transaction& transaction, std::string_view tableName, std::string_view displayTemplate)
{
    DbCrud::AddRowToTable(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableDisplayTemplateTable,
        { DbPair(DbSchema::kAdminTableDisplayTemplateName, tableName),
          DbPair(DbSchema::kAdminTableDisplayTemplateTemplate, displayTemplate) });
}

void AdminTableDisplayTemplate::DeleteDisplayTemplate(Transaction& transaction, std::string_view tableName)
{
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableDisplayTemplateTable,
        DbSchema::kAdminTableDisplayTemplateName,
        tableName);
}

KeyValueTable AdminTableDisplayTemplate::GetAllDisplayTemplates(Transaction& transaction)
{
    KeyValueTableArray rows = DbCrud::GetTableRows(
        transaction,
        databaseHelper_,
        DbSchema::kAdminTableDisplayTemplateTable);
    KeyValueTable templates;
    for (const KeyValueTable& row : rows) {
        templates[row.at(std::string(DbSchema::kAdminTableDisplayTemplateName))] =
            row.at(std::string(DbSchema::kAdminTableDisplayTemplateTemplate));
    }
    return templates;
}

} // namespace TableHelpers
