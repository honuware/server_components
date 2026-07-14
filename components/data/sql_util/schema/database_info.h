#pragma once

#include "sql_util/database_common.h"
#include "foreign_key_manager.h"
#include "table_info.h"

namespace DbSchema {

	class DatabaseInfoImpl;

	using TableInfoList = std::vector<TableInfo>;

    inline constexpr std::string_view kDatabaseInfoDefaultZero = "0";
	inline constexpr std::string_view kDatabaseInfoDefaultFalse = "FALSE";
	inline constexpr std::string_view kDatabaseInfoDefaultTrue = "TRUE";
	inline constexpr std::string_view kDatabaseInfoDefaultNow = "now_us()";
	inline constexpr std::string_view kDatabaseInfoDefaultGenRandomUuid = "GEN_RANDOM_UUID()";

	class DatabaseInfo {
	public:
		DatabaseInfo(std::string_view databaseName);
		DatabaseInfo(const DatabaseInfo&) = default;
		DatabaseInfo& operator=(const DatabaseInfo&) = default;
		~DatabaseInfo() = default;

		// Deep copy. The ordinary copy constructor / assignment share the
		// underlying pImpl (a copy reflects the original's later mutations);
		// Clone() returns a fully independent DatabaseInfo whose subsequent
		// AddTable/AddColumn calls do not affect this instance. Needed by test
		// helpers that take the shared composed schema and add per-test tables.
		DatabaseInfo Clone() const;

		// Accessors
		const std::string& GetDatabaseName() const;
		const ForeignKeyManager& GetForeignKeyManager() const;
		const TableInfo& GetTableInfo(std::string_view tableName) const;
		StringArray GetRootTables() const;
		StringArray GetAllTables() const;

		// Modifiers
		void AddTable(std::string_view tableName);
		void AddColumnSimple(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnPrimaryKey(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnUnique(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnNullable(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType);
		void AddColumnForeignKeyRef(
			std::string_view tableNameParent,
			std::string_view columnNameParent,
			std::string_view tableNameChild,
			std::string_view columnNameChild);
		void AddColumnForeignKeyRefNullable(
			std::string_view tableNameParent,
			std::string_view columnNameParent,
			std::string_view tableNameChild,
			std::string_view columnNameChild);
		void AddColumnNotNullableWithDefault(
			std::string_view tableName,
			std::string_view columnName,
			DatabaseTypes databaseType,
            std::string_view defaultValue);

		// Unique constraint helpers
		void AddUniqueConstraintHelper(
			std::string_view tableName,
			const TableUniqueValues& tableUniqueValues);

		template <class... Args>
		void AddNamedUniqueConstraint(std::string_view tableName, std::string_view name, Args&&... args) {
			AddUniqueConstraintHelper(tableName, BuildTableUniqueValues(name, std::forward<Args>(args)...));
		}

		template <class... Args>
		void AddUniqueConstraint(std::string_view tableName, Args&&... args) {
			AddUniqueConstraintHelper(tableName, BuildTableUniqueValues("", std::forward<Args>(args)...));
		}

		// Check constraint helper (name can be empty for an unnamed constraint)
		void AddCheckConstraint(
			std::string_view tableName,
			std::string_view name,
			std::string_view expression);

	private:
		std::shared_ptr<DatabaseInfoImpl> impl_;
	};

}  // namespace DbSchema
