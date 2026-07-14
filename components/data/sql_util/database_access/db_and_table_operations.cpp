#include "sql_util/database_access/db_and_table_operations.h"

namespace DbOps {

	namespace {
		bool g_ifNotExistsMode = false;
	}  // namespace

	void SetIfNotExistsMode(bool enabled) {
		g_ifNotExistsMode = enabled;
	}

	bool IsIfNotExistsMode() {
		return g_ifNotExistsMode;
	}

	std::string GenerateDropTableSql(
		std::string_view tableName) {
		std::stringstream ss;
		ss << "DROP TABLE IF EXISTS " << tableName << " CASCADE";
		return ss.str();
	}

	std::string GenerateCreateTableSql(
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName) {
		const DbSchema::TableInfo& tableInfo
			= databaseInfo.GetTableInfo(tableName);
		std::stringstream ss;
		if (g_ifNotExistsMode) {
			ss << "CREATE TABLE IF NOT EXISTS " << tableName << " (";
		} else {
			ss << "CREATE TABLE " << tableName << " (";
		}

		StringArray stringArrayFields;
		StringArray stringArrayConstraints;
		for (const DbSchema::ColumnInfo& columnInfo : tableInfo.GetColumns()) {
			std::stringstream ssField;
			ssField << columnInfo.GetColumnName() << " ";
			ssField << columnInfo.GetSqlType();
			if (columnInfo.IsPrimaryKey()) {
				ssField << " PRIMARY KEY";
			}
			else {
				if (columnInfo.IsUnique()) {
					ssField << " UNIQUE";
				}
				else if (!columnInfo.IsNullable()) {
					ssField << " NOT NULL";
				}
				std::string defaultValue;
				if(columnInfo.IsDefault(defaultValue)) {
					ssField << " DEFAULT " << defaultValue;
                }
				DbSchema::TableColumnPair tableColumnPairChild{ std::string(tableName), columnInfo.GetColumnName() };
				if (databaseInfo.GetForeignKeyManager().HasForeignReference(
					tableColumnPairChild)) {
					DbSchema::TableColumnPair tableColumnPairParent 
						= databaseInfo.GetForeignKeyManager()
						.GetForeignReferenceOfField(tableColumnPairChild);
					std::stringstream ssConstraint;
					ssConstraint << "CONSTRAINT fk_" << tableName << "_"
						<< columnInfo.GetColumnName() << " FOREIGN KEY("
						<< columnInfo.GetColumnName() << ") REFERENCES "
						<< tableColumnPairParent.tableName << "(" 
						<< tableColumnPairParent.columnName 
						<< ") ON DELETE CASCADE";
					stringArrayConstraints.push_back(ssConstraint.str());
				}
			}
			stringArrayFields.push_back(ssField.str());
		}

		// Add multi-column unique constraints
		for (const DbSchema::TableUniqueValues& tableUniqueValues
			: tableInfo.GetTableUniqueValues()) {
			std::stringstream ssConstraint;
			if (!tableUniqueValues.name.empty()) {
				ssConstraint << "CONSTRAINT " << tableUniqueValues.name << " ";
			}
			ssConstraint << "UNIQUE ("
				<< StringArrayToCommaSeparatedString(tableUniqueValues.columns)
				<< ")";
			stringArrayConstraints.push_back(ssConstraint.str());
		}

		// Add table-level check constraints
		for (const DbSchema::TableCheckConstraint& tableCheckConstraint
			: tableInfo.GetTableCheckConstraints()) {
			std::stringstream ssConstraint;
			if (!tableCheckConstraint.name.empty()) {
				ssConstraint << "CONSTRAINT " << tableCheckConstraint.name << " ";
			}
			ssConstraint << "CHECK ("
				<< tableCheckConstraint.expression
				<< ")";
			stringArrayConstraints.push_back(ssConstraint.str());
		}

		for (const std::string& constraint : stringArrayConstraints) {
			stringArrayFields.push_back(constraint);
		}

		ss << StringArrayToCommaSeparatedString(stringArrayFields) << ")";
		return ss.str();
	}

	void DropTable(
		Transaction& transaction,
		std::string_view tableName) {
		std::string sqlDropTable = GenerateDropTableSql(tableName);
		transaction.RunSqlStatement(sqlDropTable);
	}

	void CreateTable(
		Transaction& transaction,
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName) {
		std::string sqlCreateTable = GenerateCreateTableSql(databaseInfo, tableName);
		transaction.RunSqlStatement(sqlCreateTable);
	}

	void DropIfExistsAndCreateTable(
		Transaction& transaction,
		DbSchema::DatabaseInfo databaseInfo,
		std::string_view tableName) {
		std::string sqlDropTable = GenerateDropTableSql(tableName);
		std::string sqlCreateTable = GenerateCreateTableSql(databaseInfo, tableName);
        transaction.RunSqlStatement(sqlDropTable);
        transaction.RunSqlStatement(sqlCreateTable);
	}

}  // namespace DbSchema
