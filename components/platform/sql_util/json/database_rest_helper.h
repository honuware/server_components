#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "util/json_value.h"

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/table_helpers/admin_column_redactions.h"

namespace TableHelpers {
	class AdminTableFriendlyNames;
	class AdminColumnFriendlyNames;
	class AdminColumnDataInfo;
	class AdminEnumValues;
	class AdminColumnEnums;
}  // namespace TableHelpers {

class DatabaseRESTHelper {
public:
	// Phase 3.8 of the security review: the optional ColumnRedactionSet
	// is consulted on every read path (GetRow / GetTableValues /
	// GetRowsByColumn[WithCount] / GetRowsByValuesWithCount /
	// GetRowByValues / GetTableValue). Any (table, column) pair in the
	// set is silently dropped from the resulting JSON. Production
	// endpoints pass the set held by WebApp; tests may pass an empty
	// set when they want the legacy "all columns visible" behavior.
	DatabaseRESTHelper(
		DatabaseHelper databaseHelper,
		TableHelpers::ColumnRedactionSet columnRedactions = {});
	DatabaseRESTHelper(const DatabaseRESTHelper&) = default;
	DatabaseRESTHelper& operator=(const DatabaseRESTHelper&) = default;
	~DatabaseRESTHelper() = default;

	// Adds the given value as a row to a table
	void AddTableValue(
        Transaction& transaction,
		const DbSchema::DatabaseInfo& databaseInfo,
		std::string_view tableName,
		const Json::Value& jsonValue);
	// Adds the given value as a row to a table and fetches the generated key
	Json::Value AddTableValueFetchPrimaryKey(
		Transaction& transaction,
		const DbSchema::DatabaseInfo& databaseInfo,
		std::string_view tableName,
		const Json::Value& jsonValue);

	// Convert the given table into JSON string values with keys
	Json::Value GetTableValues(
		Transaction& transaction, std::string_view tableName);

	/*
	{
	  "sortedColumnNames": ["column1", ...],
	  "dataTable":
	  [
        ["value1a", "value1b", ...],
		...,
		["valueNa", "valueNb", ...]
	  ]
    }
	*/
	Json::Value GetRowsByColumn(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view columnName,
		bool asc,
		int pageSize,
		int page);

	/*
	{
	  "sortedColumnNames": ["column1", ...],
	  "dataTable":
	  [
        ["value1a", "value1b", ...],
		...,
		["valueNa", "valueNb", ...]
	  ],
	  "totalCount": 47
    }
	*/
	Json::Value GetRowsByColumnWithCount(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view columnName,
		bool asc,
		int pageSize,
		int page);

	// Filtered, sorted, paginated rows with total count.
	// When lookupValues is empty, equivalent to GetRowsByColumnWithCount.
	Json::Value GetRowsByValuesWithCount(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view sortColumnName,
		const KeyValueTable& lookupValues,
		bool asc,
		int pageSize,
		int page);

    // Fetch a single row. Gets packed into DataResults format
    // even though it only has a single row.
	Json::Value GetRow(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view columnName,
        std::string_view value);

    // Fetch a single row by multiple column values.
    // Gets packed into DataResults format.
	Json::Value GetRowByValues(
		Transaction& transaction,
		std::string_view tableName,
		const KeyValueTable& lookupValues);
	
	// Fetch a single value
	Json::Value GetTableValue(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view columnName,
		std::string_view value);

	// Update the row matching the given column name matching the value with
	// provided data.
	void UpdateTableValue(
		Transaction& transaction,
		const DbSchema::DatabaseInfo& databaseInfo,
		std::string_view tableName,
		std::string_view columnName,
		std::string_view value,
		const Json::Value& jsonValue);

	// Update the row matching the given column name matching the value with
	// provided data.
	void DeleteTableValue(
		Transaction& transaction,
		const DbSchema::DatabaseInfo& databaseInfo,
		std::string_view tableName,
		std::string_view columnName,
		std::string_view value);

	// Search for FK options with display text resolution.
	// Returns { "total_count": N, "options": [{ "value": "pk", "display": "..." }, ...] }
	Json::Value GetFkOptions(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view primaryKeyColumn,
		std::string_view displayTemplate,
		std::string_view searchText,
		int pageSize);

	// Batch-resolve FK values to display text.
	// Returns { "resolved": { "pk1": "display text 1", "pk2": "display text 2", ... } }
	Json::Value ResolveFkDisplay(
		Transaction& transaction,
		std::string_view tableName,
		std::string_view primaryKeyColumn,
		std::string_view displayTemplate,
		const StringArray& values);

	// Convert the database metadata to JSON
	/*
	{
	  "root_tables": ["root_tables"*],
	  "tables": [
		{
		  "columns": [
			{
			  "column_friendly_name": "...",
			  "column_name": "...",
			  "default_value": "...",
			  "hint": "...",
			  "html_input_type": "...",
			  "label": "...",
			  "max_length": "int max length",
			  "nullable": "bool nullable",
			  "place_holder": "...",
			  "primary_key": "bool if primary key",
			  "regex": "...",
			  "required": "bool if this field is required",
			  "rows": "int number of rows",
			  "type": "SQL type",
			  "unique": "bool if this is unique"
			}*
		  "description": "...",
		  "foreign_keys": [
			{
			  "column_name": "column in this table",
			  "parent_column_name": "column referenced in parent",
			  "parent_table_name": "table referenced"
			}
		  ],
		  "primary_key": "...",
		  "table_friendly_name": "...",
		  "table_name": "..."
		}
	  ]
	}
	*/
	Json::Value DatabaseMetadata(
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
		const KeyValueTable& displayTemplates = {},
		int fkPickerPreloadThreshold = 50);

private:
	// Strip redacted columns from a single row before it leaves
	// DatabaseRESTHelper. Mutates in place.
	void ApplyRedactions(
		std::string_view tableName, KeyValueTable& row) const;
	void ApplyRedactions(
		std::string_view tableName, KeyValueTableArray& rows) const;

	DatabaseHelper databaseHelper_;
	TableHelpers::ColumnRedactionSet columnRedactions_;
};