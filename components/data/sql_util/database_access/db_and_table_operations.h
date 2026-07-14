#pragma once

#include "sql_util/database_common.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/transaction.h"

namespace DbOps {

	// When enabled, GenerateCreateTableSql produces CREATE TABLE IF NOT EXISTS.
	// Used by the test infrastructure so that pre-created committed tables
	// are silently skipped during per-test table setup.
	void SetIfNotExistsMode(bool enabled);
	bool IsIfNotExistsMode();

	// Table SQL generations
	std::string GenerateDropTableSql(
		std::string_view tableName);
	std::string GenerateCreateTableSql(
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName);

	// Table management
	void DropTable(Transaction& transaction, std::string_view tableName);
	void CreateTable(
		Transaction& transaction,
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName);
	void DropIfExistsAndCreateTable(
		Transaction& transaction,
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName);

}  // namespace DbSchema
