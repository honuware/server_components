#include "database_rest_helper.h"

#include "util/json_util.h"
#include "util/json_value.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_metadata.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/table_helpers/admin_table_friendly_names.h"
#include "db_schema/admin_table_friendly_names.h"
#include "sql_util/table_helpers/admin_column_friendly_names.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_enum_values.h"
#include "sql_util/table_helpers/admin_column_enums.h"
#include "db_schema/admin_column_data_info.h"
#include "db_schema/admin_column_enums.h"
#include "db_schema/admin_enum_values.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "db_schema/photos.h"

namespace {

    StringSet StringSetFromColumnInfoList(
        const DbSchema::ColumnInfoList& columnInfoList) {
        StringSet stringSet;
        for (const DbSchema::ColumnInfo& columnInfo : columnInfoList) {
            stringSet.insert(columnInfo.GetColumnName());
        }
        return stringSet;
    }

    KeyValueTable KeyValueTableFromJsonValue(
        const DbSchema::DatabaseInfo& databaseInfo,
        std::string_view tableName,
        const Json::Value& jsonValue) {
        KeyValueTable keyValueTable;
        const DbSchema::TableInfo& tableInfo =
            databaseInfo.GetTableInfo(tableName);
        const DbSchema::ColumnInfoList& columnInfoList = tableInfo.GetColumns();
        StringSet columnsStringSet = StringSetFromColumnInfoList(columnInfoList);
        if (!jsonValue.HasChildren()) {
            throw std::invalid_argument("DatabaseRESTHelper::AddTableValue - expected JSON object");
        }
        for (const auto& [key, value] : jsonValue.GetChildren()) {
            if (!SetContains(columnsStringSet, key)) {
                std::stringstream ss;
                ss << "DatabaseRESTHelper::AddTableValue - invalid key: "
                    << key;
                throw std::invalid_argument(ss.str());
            }
            if(value.Is<std::string>()) {
                keyValueTable.insert({ key, value.Get<std::string>() });
                continue;
            }
            keyValueTable.insert({ key, value.ToSimpleString()});
        }
        return keyValueTable;
    }

    Json::Value GenerateColumnMetadata(
        Transaction& transaction,
        const DbSchema::ColumnInfo& columnInfo,
        std::string_view tableName,
        TableHelpers::AdminColumnFriendlyNames& adminColumnFriendlyNames,
        TableHelpers::AdminColumnDataInfo& adminColumnDataInfo,
        TableHelpers::AdminEnumValues& adminEnumValues,
        TableHelpers::AdminColumnEnums& adminColumnEnums) {
        Json::Value column;
        column["column_name"] = columnInfo.GetColumnName();
        column["type"] = columnInfo.GetSqlType();
        column["primary_key"] = Json::StringFromBool(columnInfo.IsPrimaryKey());
        column["unique"] = Json::StringFromBool(columnInfo.IsUnique());
        column["nullable"] = Json::StringFromBool(columnInfo.IsNullable());
        column["column_friendly_name"] =
            adminColumnFriendlyNames.GetAdminColumnFriendlyName(
                transaction, tableName, columnInfo.GetColumnName());
        KeyValueTable keyValueTable;
        if(adminColumnDataInfo.GetAdminColumnDataInfo(
            transaction, tableName, columnInfo.GetColumnName(), keyValueTable)) {
            column["label"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoLabel));
            column["hint"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoHint));
            column["place_holder"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoPlaceHolder));
            column["regex"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoRegex));
            column["html_input_type"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoHtmlInputType));
            column["required"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoRequired));
            column["max_length"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoMaxLength));
            column["default_value"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoDefaultValue));
            column["rows"] = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoRows));
            std::string hiddenVal = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoHidden));
            column["hidden"] = hiddenVal.empty() ? "f" : hiddenVal;
            std::string readonlyVal = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoReadonly));
            column["readonly"] = readonlyVal.empty() ? "f" : readonlyVal;

            // Check for enum binding
            int64_t columnDataInfoId = std::stoll(keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminColumnDataInfoColumnDataInfoId)));
            KeyValueTable enumBinding;
            if (adminColumnEnums.GetAdminColumnEnumByColumnDataInfoId(
                    transaction, columnDataInfoId, enumBinding)) {
                column["html_input_type"] = "enum";
                int64_t enumId = std::stoll(enumBinding.at(
                    static_cast<std::string>(DbSchema::kAdminColumnEnumsAdminEnumId)));
                KeyValueTableArray enumValues =
                    adminEnumValues.GetAdminEnumValuesByEnumId(transaction, enumId);
                Json::JsonArray enumValuesArray;
                for (const auto& enumValue : enumValues) {
                    enumValuesArray.push_back(Json::Value(
                        enumValue.at(static_cast<std::string>(DbSchema::kAdminEnumValuesName))));
                }
                column["enum_values"] = Json::Value(std::move(enumValuesArray));
            }
        } else {
            column["label"] = "";
            column["hint"] = "";
            column["place_holder"] = "";
            column["regex"] = "";
            column["html_input_type"] = "";
            column["required"] = "f";
            column["max_length"] = "";
            column["default_value"] = "";
            column["rows"] = "1";
            column["hidden"] = "f";
            column["readonly"] = "f";
        }
        return column;
    }

    Json::Value GenerateColumnArrayMetadata(
        Transaction& transaction,
        const DbSchema::TableInfo& tableInfo,
        TableHelpers::AdminColumnFriendlyNames& adminColumnFriendlyNames,
        TableHelpers::AdminColumnDataInfo& adminColumnDataInfo,
        TableHelpers::AdminEnumValues& adminEnumValues,
        TableHelpers::AdminColumnEnums& adminColumnEnums) {
        Json::Value columnArray;
        std::vector<Json::Value> columnList;
        for (const DbSchema::ColumnInfo& columnInfo : tableInfo.GetColumns()) {
            columnList.push_back(GenerateColumnMetadata(
                transaction,
                columnInfo,
                tableInfo.GetTableName(),
                adminColumnFriendlyNames,
                adminColumnDataInfo,
                adminEnumValues,
                adminColumnEnums));
        }
        columnArray = std::move(columnList);
        return columnArray;
    }

    Json::Value GenerateForeignKeyRefMetadata(
        std::string_view columnName,
        std::string_view parentTableName,
        std::string_view parentColumnName) {
        Json::Value foreignKeyRef;
        foreignKeyRef["column_name"] = std::string(columnName);
        foreignKeyRef["parent_column_name"] = std::string(parentColumnName);
        foreignKeyRef["parent_table_name"] = std::string(parentTableName);
        return foreignKeyRef;
    }

    Json::Value GenerateForeignKeyArrayMetadata(
        const DbSchema::TableInfo& tableInfo,
        const DbSchema::ForeignKeyManager& foreignKeyManager) {
        Json::Value foreignKeyRefArray;
        std::vector<Json::Value> foreignKeyRefList;
        for (const DbSchema::ColumnInfo& columnInfo : tableInfo.GetColumns()) {
            DbSchema::TableColumnPair tableColumnPair{ 
                tableInfo.GetTableName(), columnInfo.GetColumnName() };
            if (foreignKeyManager.HasForeignReference(tableColumnPair)) {
                const auto& [
                    parentTableName, 
                    parentColumnName] =
                    foreignKeyManager.GetForeignReferenceOfField(
                        tableColumnPair);
                foreignKeyRefList.push_back(
                    GenerateForeignKeyRefMetadata(
                        columnInfo.GetColumnName(), 
                        parentTableName, 
                        parentColumnName));
            }
        }
        foreignKeyRefArray = std::move(foreignKeyRefList);
        return foreignKeyRefArray;
    }

    Json::Value GenerateTableMetadata(
        Transaction& transaction,
        const DbSchema::DatabaseInfo& databaseInfo,
        TableHelpers::AdminTableFriendlyNames& adminTableFriendlyNames,
        TableHelpers::AdminColumnFriendlyNames& adminColumnFriendlyNames,
        TableHelpers::AdminColumnDataInfo& adminColumnDataInfo,
        TableHelpers::AdminEnumValues& adminEnumValues,
        TableHelpers::AdminColumnEnums& adminColumnEnums,
        std::string_view tableName,
        const StringSet& photoSupportedTables) {
        Json::Value table;
        const DbSchema::ForeignKeyManager& foreignKeyManager =
            databaseInfo.GetForeignKeyManager();
        const DbSchema::TableInfo& tableInfo =
            databaseInfo.GetTableInfo(tableName);
        std::string tableFriendlyName;
        std::string description;
        KeyValueTable keyValueTable;
        if (adminTableFriendlyNames.GetAdminTableFriendlyName(
            transaction, tableName, keyValueTable) && !keyValueTable.empty()) {
            tableFriendlyName = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminTableFriendlyNamesFriendlyName));
            description = keyValueTable.at(
                static_cast<std::string>(DbSchema::kAdminTableFriendlyNamesDescription));
        }
        else {
            tableFriendlyName = tableInfo.GetTableName();
        }
        table["columns"] = GenerateColumnArrayMetadata(
            transaction, tableInfo, adminColumnFriendlyNames, adminColumnDataInfo,
            adminEnumValues, adminColumnEnums);
        table["description"] = description;
        table["foreign_keys"] = GenerateForeignKeyArrayMetadata(
            tableInfo, foreignKeyManager);
        table["primary_key"] = tableInfo.GetPrimaryKey();
        table["table_friendly_name"] = tableFriendlyName;
        table["table_name"] = std::string(tableName);
        if (SetContains(photoSupportedTables, std::string(tableName))) {
            table["has_photo_support"] = Json::StringFromBool(true);
        }
        return table;
    }

    Json::Value GenerateTableArrayMetadata(
        Transaction& transaction,
        const DbSchema::DatabaseInfo& databaseInfo,
        TableHelpers::AdminTableFriendlyNames& adminTableFriendlyNames,
        TableHelpers::AdminColumnFriendlyNames& adminColumnFriendlyNames,
        TableHelpers::AdminColumnDataInfo& adminColumnDataInfo,
        TableHelpers::AdminEnumValues& adminEnumValues,
        TableHelpers::AdminColumnEnums& adminColumnEnums,
        const StringSet& allowedTablesStringSet,
        const StringSet& photoSupportedTables) {
        Json::Value tableArray;
        std::vector<Json::Value> tableList;
        for (const std::string& tableName : databaseInfo.GetAllTables()) {
            if (SetContains(allowedTablesStringSet, tableName)) {
                tableList.push_back(GenerateTableMetadata(
                    transaction,
                    databaseInfo,
                    adminTableFriendlyNames,
                    adminColumnFriendlyNames,
                    adminColumnDataInfo,
                    adminEnumValues,
                    adminColumnEnums,
                    tableName,
                    photoSupportedTables));
            }
        }
        tableArray = std::move(tableList);
        return tableArray;
    }

    Json::Value GenerateRootTablesArrayMetadata(
        const StringArray& rootTables,
        const StringSet& allowedTablesStringSet) {
        Json::Value rootTablesArray;
        std::vector<Json::Value> rootTablesList;
        for (const std::string& tableName : rootTables) {
            if (SetContains(allowedTablesStringSet, tableName)) {
                rootTablesList.push_back(Json::Value(tableName));
            }
        }
        rootTablesArray = std::move(rootTablesList);
        return rootTablesArray;
    }

    Json::Value GenerateNestedTablesArrayMetadata(
        const StringArray& nestedTables,
        const StringSet& allowedTablesStringSet) {
        Json::Value nestedTablesArray;
        std::vector<Json::Value> nestedTablesList;
        for (const std::string& tableName : nestedTables) {
            if (SetContains(allowedTablesStringSet, tableName)) {
                nestedTablesList.push_back(Json::Value(tableName));
            }
        }
        nestedTablesArray = std::move(nestedTablesList);
        return nestedTablesArray;
    }


}  // namespace

DatabaseRESTHelper::DatabaseRESTHelper(
    DatabaseHelper databaseHelper,
    TableHelpers::ColumnRedactionSet columnRedactions /* = {} */)
    : databaseHelper_(databaseHelper),
      columnRedactions_(std::move(columnRedactions)) {
}

void DatabaseRESTHelper::ApplyRedactions(
    std::string_view tableName, KeyValueTable& row) const {
    if (columnRedactions_.empty()) return;
    std::string tableKey(tableName);
    for (auto it = row.begin(); it != row.end(); ) {
        if (columnRedactions_.count({ tableKey, it->first }) > 0) {
            it = row.erase(it);
        } else {
            ++it;
        }
    }
}

void DatabaseRESTHelper::ApplyRedactions(
    std::string_view tableName, KeyValueTableArray& rows) const {
    if (columnRedactions_.empty()) return;
    for (KeyValueTable& row : rows) {
        ApplyRedactions(tableName, row);
    }
}

void DatabaseRESTHelper::AddTableValue(
    Transaction& transaction,
    const DbSchema::DatabaseInfo& databaseInfo,
    std::string_view tableName,
    const Json::Value& jsonValue) {
    KeyValueTable keyValueTable = KeyValueTableFromJsonValue(
        databaseInfo, tableName, jsonValue);
    DbCrud::AddRowToTable(transaction, databaseHelper_, tableName, keyValueTable);
}

Json::Value DatabaseRESTHelper::AddTableValueFetchPrimaryKey(
    Transaction& transaction,
    const DbSchema::DatabaseInfo& databaseInfo,
    std::string_view tableName,
    const Json::Value& jsonValue) {
    const DbSchema::TableInfo& tableInfo =
        databaseInfo.GetTableInfo(tableName);
    if (!tableInfo.IsPrimaryKey()) {
        std::stringstream ss;
        ss << "DatabaseRESTHelper::AddTableValueFetchPrimaryKey called on "
            << "table with no primary key: " << tableName;
        throw std::invalid_argument(ss.str());
    }
    KeyValueTable keyValueTable = KeyValueTableFromJsonValue(
        databaseInfo, tableName, jsonValue);
    int64_t id = DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper_, tableName, keyValueTable);
    std::stringstream ss;
    ss << id;
    Json::JsonObject value;
    value[tableInfo.GetPrimaryKey()] = ss.str();
    return Json::Value(std::move(value));
}


Json::Value DatabaseRESTHelper::GetTableValues(
    Transaction& transaction, std::string_view tableName) {
    KeyValueTableArray keyValueTableArray = DbCrud::GetTableRows(
        transaction, databaseHelper_, tableName);
    ApplyRedactions(tableName, keyValueTableArray);
    DataResults dataResults = MakeDataResultsFromKeyValueTableArray(
        keyValueTableArray);
    return Json::JsonFromDataResults(dataResults);
}

Json::Value DatabaseRESTHelper::GetRowsByColumn(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    bool asc,
    int pageSize,
    int page) {
    KeyValueTableArray keyValueTableArray = DbCrud::GetRowsByColumn(
        transaction, databaseHelper_, tableName,
        columnName, asc, pageSize, page);
    ApplyRedactions(tableName, keyValueTableArray);
    DataResults dataResults = MakeDataResultsFromKeyValueTableArray(keyValueTableArray);
    return Json::JsonFromDataResults(dataResults);
}

Json::Value DatabaseRESTHelper::GetRowsByColumnWithCount(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    bool asc,
    int pageSize,
    int page) {
    KeyValueTableArray keyValueTableArray = DbCrud::GetRowsByColumn(
        transaction, databaseHelper_, tableName,
        columnName, asc, pageSize, page);
    ApplyRedactions(tableName, keyValueTableArray);
    int64_t totalCount = DbCrud::GetTableRowCount(
        transaction, databaseHelper_, tableName);
    DataResultsWithCount dataResultsWithCount;
    dataResultsWithCount.dataResults = MakeDataResultsFromKeyValueTableArray(keyValueTableArray);
    dataResultsWithCount.totalCount = totalCount;
    return Json::JsonFromDataResultsWithCount(dataResultsWithCount);
}

Json::Value DatabaseRESTHelper::GetRowsByValuesWithCount(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view sortColumnName,
    const KeyValueTable& lookupValues,
    bool asc,
    int pageSize,
    int page) {
    int64_t totalCount = 0;
    KeyValueTableArray keyValueTableArray = DbCrud::GetRowsByValues(
        transaction, databaseHelper_, tableName,
        sortColumnName, lookupValues, asc, pageSize, page, &totalCount);
    ApplyRedactions(tableName, keyValueTableArray);
    DataResultsWithCount dataResultsWithCount;
    dataResultsWithCount.dataResults = MakeDataResultsFromKeyValueTableArray(keyValueTableArray);
    dataResultsWithCount.totalCount = totalCount;
    return Json::JsonFromDataResultsWithCount(dataResultsWithCount);
}

Json::Value DatabaseRESTHelper::GetRow(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view value) {
    KeyValueTable keyValueTable = DbCrud::GetRow(
        transaction, databaseHelper_, tableName, columnName, value);
    ApplyRedactions(tableName, keyValueTable);
    DataResults dataResults = MakeDataResults(keyValueTable);
    return Json::JsonFromDataResults(dataResults);
}

Json::Value DatabaseRESTHelper::GetRowByValues(
    Transaction& transaction,
    std::string_view tableName,
    const KeyValueTable& lookupValues) {
    KeyValueTable keyValueTable = DbCrud::LookupRowByValues(
        transaction, databaseHelper_, tableName, lookupValues);
    ApplyRedactions(tableName, keyValueTable);
    DataResults dataResults = MakeDataResults(keyValueTable);
    return Json::JsonFromDataResults(dataResults);
}

Json::Value DatabaseRESTHelper::GetTableValue(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view value) {
    Json::Value result;
    KeyValueTable keyValueTable = DbCrud::LookupRowByValue(
        transaction, databaseHelper_, tableName, columnName, value);
    ApplyRedactions(tableName, keyValueTable);
    for (const auto& [key, value] : keyValueTable) {
        result[key] = value;
    }
    return result;
}

void DatabaseRESTHelper::UpdateTableValue(
    Transaction& transaction,
    const DbSchema::DatabaseInfo& databaseInfo,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view value,
    const Json::Value& jsonValue) {
    KeyValueTable keyValueTable = KeyValueTableFromJsonValue(
        databaseInfo, tableName, jsonValue);

    // Separate empty string values — these should become SQL NULL.
    // Empty strings cannot be cast to non-text types (BIGINT, etc.)
    // and generally represent "no value" intent from the client.
    StringArray nullColumns;
    for (auto it = keyValueTable.begin(); it != keyValueTable.end(); ) {
        if (it->second.empty()) {
            nullColumns.push_back(it->first);
            it = keyValueTable.erase(it);
        } else {
            ++it;
        }
    }

    if (!keyValueTable.empty()) {
        DbCrud::UpdateRow(
            transaction, databaseHelper_, tableName, columnName, value,
            keyValueTable);
    }

    for (const auto& col : nullColumns) {
        ExecParamsHelper params;
        std::string idParam = params.AddParam(value);
        std::string sql = "UPDATE ";
        sql += EscSqlTableName(databaseHelper_, tableName);
        sql += " SET ";
        sql += EscSqlColumnName(databaseHelper_, col);
        sql += " = NULL WHERE ";
        sql += EscSqlColumnName(databaseHelper_, columnName);
        sql += " = ";
        sql += idParam;
        transaction.RunSqlStatementHelper(sql, params);
    }
}

void DatabaseRESTHelper::DeleteTableValue(
    Transaction& transaction,
    const DbSchema::DatabaseInfo& databaseInfo,
    std::string_view tableName,
    std::string_view columnName,
    std::string_view value) {
    DbCrud::DeleteRow(transaction, databaseHelper_, tableName, columnName, value);
}

Json::Value DatabaseRESTHelper::GetFkOptions(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view primaryKeyColumn,
    std::string_view displayTemplate,
    std::string_view searchText,
    int pageSize) {

    KeyValueTableArray rows;
    int64_t totalCount = 0;

    if (searchText.empty()) {
        // No search text — return first pageSize rows sorted by primary key
        rows = DbCrud::GetRowsByValues(
            transaction, databaseHelper_, tableName, primaryKeyColumn,
            {}, true, pageSize, 0, &totalCount);
    } else {
        // Parse template to find searchable columns
        StringArray searchColumns = DbCrud::ParseTemplateColumns(displayTemplate);
        if (searchColumns.empty()) {
            searchColumns.push_back(std::string(primaryKeyColumn));
        }
        rows = DbCrud::SearchRowsByILike(
            transaction, databaseHelper_, tableName, primaryKeyColumn,
            searchColumns, searchText, true, pageSize, 0, &totalCount);
    }

    // Resolve display text for each row
    KeyValueTable displayValues = DbCrud::ResolveDisplayValues(
        rows, primaryKeyColumn, displayTemplate);

    // Build response
    Json::JsonArray options;
    std::string pkCol(primaryKeyColumn);
    for (const KeyValueTable& row : rows) {
        auto it = row.find(pkCol);
        if (it == row.end()) continue;
        const std::string& pkValue = it->second;
        Json::JsonObject option;
        option["value"] = pkValue;
        option["display"] = displayValues[pkValue];
        options.push_back(Json::Value(std::move(option)));
    }

    Json::JsonObject result;
    result["total_count"] = totalCount;
    result["options"] = Json::Value(std::move(options));
    return Json::Value(std::move(result));
}

Json::Value DatabaseRESTHelper::ResolveFkDisplay(
    Transaction& transaction,
    std::string_view tableName,
    std::string_view primaryKeyColumn,
    std::string_view displayTemplate,
    const StringArray& values) {

    KeyValueTableArray rows = DbCrud::LookupRowsByPrimaryKeyValues(
        transaction, databaseHelper_, tableName, primaryKeyColumn, values);

    KeyValueTable displayValues = DbCrud::ResolveDisplayValues(
        rows, primaryKeyColumn, displayTemplate);

    Json::JsonObject resolved;
    for (const auto& [pkValue, displayText] : displayValues) {
        resolved[pkValue] = displayText;
    }

    Json::JsonObject result;
    result["resolved"] = Json::Value(std::move(resolved));
    return Json::Value(std::move(result));
}

Json::Value DatabaseRESTHelper::DatabaseMetadata(
    Transaction& transaction,
    const DbSchema::DatabaseInfo& databaseInfo,
    const StringArray& allowedTables,
    const StringArray& rootTables,
    const StringArray& nestedTables,
    TableHelpers::AdminTableFriendlyNames& adminTableFriendlyNames,
    TableHelpers::AdminColumnFriendlyNames& adminColumnFriendlyNames,
    TableHelpers::AdminColumnDataInfo& adminColumnDataInfo,
    TableHelpers::AdminEnumValues& adminEnumValues,
    TableHelpers::AdminColumnEnums& adminColumnEnums,
    const KeyValueTable& displayTemplates /*= {}*/,
    int fkPickerPreloadThreshold /*= 50*/) {
    Json::Value databaseMetadata;
    StringSet stringSetAllowedTables = StringSetFromStringArray(
        allowedTables);

    // Query photo-supported tables (safely check if table exists first)
    StringSet photoSupportedTables;
    std::string photoTableExists =
        transaction.RunSqlStatementReturningOneValue(
            "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
            "WHERE table_schema = 'public' "
            "AND table_name = 'photo_support_tables')");
    if (photoTableExists == "t") {
        TableHelpers::PhotoSupportTables photoSupportHelper(databaseHelper_);
        KeyValueTableArray allPhotoTables =
            photoSupportHelper.GetAllPhotoSupportTables(transaction);
        for (const auto& row : allPhotoTables) {
            photoSupportedTables.insert(row.at(
                static_cast<std::string>(DbSchema::kPhotoSupportTablesTableName)));
        }
    }

    databaseMetadata["nested_tables"] = GenerateNestedTablesArrayMetadata(
        nestedTables, stringSetAllowedTables);
    databaseMetadata["root_tables"] = GenerateRootTablesArrayMetadata(
        rootTables, stringSetAllowedTables);
    databaseMetadata["tables"] = GenerateTableArrayMetadata(
        transaction,
        databaseInfo,
        adminTableFriendlyNames,
        adminColumnFriendlyNames,
        adminColumnDataInfo,
        adminEnumValues,
        adminColumnEnums,
        stringSetAllowedTables,
        photoSupportedTables);

    // Display templates for FK picker
    Json::JsonObject displayTemplatesJson;
    for (const auto& [tableName, templateStr] : displayTemplates) {
        displayTemplatesJson[tableName] = templateStr;
    }
    databaseMetadata["display_templates"] =
        Json::Value(std::move(displayTemplatesJson));
    databaseMetadata["fk_picker_preload_threshold"] =
        static_cast<int64_t>(fkPickerPreloadThreshold);

    return databaseMetadata;
}


