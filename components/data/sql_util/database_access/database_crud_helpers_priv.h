#pragma once

#include "sql_util/database_access/database_helper.h"

#include "database_util.h"

namespace DbCrud {

    namespace PrivateSql {

        // Generates the SQL statement to add a row to a table
        // with a provided primary key
        std::string GenerateAddRowToTableSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper);

        // Generate the SQL statement to add a row to table
        // and to fetch the generated primary key
        std::string GenerateAddRowToTableFetchIntPrimaryKeySql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper);

        // Generate SQl to fetch row looking up by value
        std::string GenerateLookupRowByValueSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view lookupValue);

        std::string GenerateLookupRowByValuesSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            KeyValueTable lookupValues,
            ExecParamsHelper& execParamsHelper);

        // Generate SQL to do pagination
        std::string GenerateGetRowsByColumnSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            bool asc,
            int pageSize,
            int page);

        // Generate SQL for filtered, sorted, paginated query with count
        std::string GenerateGetRowsByValuesSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view sortColumnName,
            const KeyValueTable& lookupValues,
            bool asc,
            int pageSize,
            int page,
            ExecParamsHelper& execParamsHelper);

        // Generate SQL for filtered query with ORDER BY (no pagination)
        std::string GenerateGetRowsByValuesWithOrderBySql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& lookupValues,
            std::string_view orderByColumn,
            bool asc,
            ExecParamsHelper& execParamsHelper);

        // Generate SQL for ILIKE search across multiple columns with count
        std::string GenerateSearchRowsByILikeSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view sortColumnName,
            const StringArray& searchColumns,
            std::string_view searchText,
            bool asc,
            int pageSize,
            int page,
            ExecParamsHelper& execParamsHelper);

        // Generate SQL to feth a single row
        std::string GenerateGetRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue,
            StringArray& saColumnNames);

        // Generate the SQL statement to update a row in a table
        std::string GenerateUpdateRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper);

        // Generate the SQL statement to delete a row in a table
        std::string GenerateDeleteRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue);

        // Generate the SQL statement to delete a row in a table
        std::string GenerateDeleteRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& keyValueTable,
            ExecParamsHelper& execParamsHelper);

    }  // namespace PrivateSql
}  //  namespace DbCrud