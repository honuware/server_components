#pragma once

#include "sql_util/database_access/database_helper.h"

namespace DbCrud {

    // Adds row to table with provided primary key
    void AddRowToTable(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords = StringSet());

    // Adds row to table and fetches the generated primary key
    int AddRowToTableFetchIntPrimaryKey(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords = StringSet());

    // Adds row to table and fetches the generated primary key as int64_t
    int64_t AddRowToTableFetchInt64PrimaryKey(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords = StringSet());

    // Fetches a row from the table with the given value
    KeyValueTable LookupRowByValue(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view lookupValue);

    KeyValueTable LookupRowByValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        KeyValueTable lookupValues);

    KeyValueTableArray GetTableRows(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName);

    KeyValueTableArray GetRowsByColumn(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        bool asc,
        int pageSize,
        int page);

    // Returns paginated, sorted, filtered rows. Combines WHERE filtering
    // (from lookupValues) with ORDER BY + LIMIT/OFFSET pagination.
    // Uses COUNT(*) OVER() window function for totalCount.
    // When lookupValues is empty, no WHERE clause — equivalent to GetRowsByColumn.
    KeyValueTableArray GetRowsByValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view sortColumnName,
        const KeyValueTable& lookupValues,
        bool asc,
        int pageSize,
        int page,
        int64_t* totalCount = nullptr);

    // Returns filtered rows sorted by a single column (no pagination).
    // lookupValues must not be empty.
    KeyValueTableArray GetRowsByValuesWithOrderBy(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& lookupValues,
        std::string_view orderByColumn,
        bool asc);

    // Returns the total number of rows in a table
    int64_t GetTableRowCount(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName);

    KeyValueTable GetRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue);

    // Update a row in the table based on the given value
    void UpdateRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords = StringSet());

    // Delete a row in the table based on the given value
    void DeleteRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue);

    // Delete a row in the table based on the given values
    void DeleteRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& keyValueTable);

    // Searches rows where any of the specified columns match the search text
    // using case-insensitive ILIKE. Returns paginated results with total count.
    KeyValueTableArray SearchRowsByILike(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view sortColumnName,
        const StringArray& searchColumns,
        std::string_view searchText,
        bool asc,
        int pageSize,
        int page,
        int64_t* totalCount = nullptr);

    // Parses a display template like "{first_name} {last_name}" and extracts
    // the column names referenced in it (e.g., ["first_name", "last_name"]).
    StringArray ParseTemplateColumns(std::string_view displayTemplate);

    // Resolves display template text for each row using FormatString.
    // Returns a map from primary key value → resolved display text.
    // If displayTemplate is empty, primary key values map to themselves.
    KeyValueTable ResolveDisplayValues(
        const KeyValueTableArray& rows,
        std::string_view primaryKeyColumn,
        std::string_view displayTemplate);

    // Fetches rows where the primary key column is in the given set of values.
    // Uses a parameterized WHERE pk IN ($1, $2, ...) query.
    KeyValueTableArray LookupRowsByPrimaryKeyValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view primaryKeyColumn,
        const StringArray& values);

}  //  namespace DbCrud