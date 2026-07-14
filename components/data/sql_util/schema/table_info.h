#pragma once

#include <string_view>

#include "sql_util/database_common.h"
#include "column_info.h"

namespace DbSchema {

	using ColumnInfoList = std::vector<ColumnInfo>;

	struct TableUniqueValues {
		std::string name;        // Constraint name (can be empty for auto-generated)
		StringArray columns;     // List of column names in the constraint

		bool operator==(const TableUniqueValues& rhs) const {
			return name == rhs.name && columns == rhs.columns;
		}
	};

	using TableUniqueValuesArray = std::vector<TableUniqueValues>;

	struct TableCheckConstraint {
		std::string name;        // Constraint name (can be empty for auto-generated)
		std::string expression;  // Boolean SQL expression emitted inside CHECK (...)

		bool operator==(const TableCheckConstraint& rhs) const {
			return name == rhs.name && expression == rhs.expression;
		}
	};

	using TableCheckConstraintArray = std::vector<TableCheckConstraint>;

	template <class... Args>
	TableUniqueValues BuildTableUniqueValues(std::string_view name, Args&&... args) {
		static_assert((std::is_convertible_v<Args, std::string_view> && ...),
			"All arguments must be convertible to std::string_view");

		TableUniqueValues result;
		result.name = std::string(name);
		(result.columns.push_back(std::string(std::forward<Args>(args))), ...);
		return result;
	}

	class TableInfoImpl;

	class TableInfo {
	public:
		TableInfo(std::string_view tableName);
		TableInfo(const TableInfo&) = default;
		TableInfo& operator=(const TableInfo&) = default;
		~TableInfo() = default;

		// Deep copy. The ordinary copy shares the pImpl (a copy reflects the
		// original's later column/constraint additions); Clone() returns a fully
		// independent TableInfo. Used by DatabaseInfo::Clone().
		TableInfo Clone() const;

		void AddColumn(const ColumnInfo& columnInfo);
		const ColumnInfoList& GetColumns() const;
		bool IsPrimaryKey() const;
		const std::string& GetPrimaryKey() const;
		bool IsColumn(std::string_view columnName) const;
		const ColumnInfo& GetColumn(std::string_view columnName) const;
		const std::string& GetTableName() const;

		// Unique constraint helpers
		void AddUniqueConstraintHelper(const TableUniqueValues& tableUniqueValues);
		const TableUniqueValuesArray& GetTableUniqueValues() const;

		template <class... Args>
		void AddNamedUniqueConstraint(std::string_view name, Args&&... args) {
			AddUniqueConstraintHelper(BuildTableUniqueValues(name, std::forward<Args>(args)...));
		}

		template <class... Args>
		void AddUniqueConstraint(Args&&... args) {
			AddUniqueConstraintHelper(BuildTableUniqueValues("", std::forward<Args>(args)...));
		}

		// Check constraint helpers
		void AddCheckConstraint(
			std::string_view name, std::string_view expression);
		const TableCheckConstraintArray& GetTableCheckConstraints() const;

	private:
		std::shared_ptr<TableInfoImpl> impl_;
	};

	inline bool operator==(const TableInfo& table1, const TableInfo& table2) {
		if (table1.GetTableName() != table2.GetTableName()) return false;
		if (table1.GetColumns().size() != table2.GetColumns().size())
			return false;
		if (!std::equal(
			table1.GetColumns().begin(),
			table1.GetColumns().end(),
			table2.GetColumns().begin())) {
			return false;
		}
		if (table1.GetTableUniqueValues().size() != table2.GetTableUniqueValues().size())
			return false;
		if (!std::equal(
			table1.GetTableUniqueValues().begin(),
			table1.GetTableUniqueValues().end(),
			table2.GetTableUniqueValues().begin())) {
			return false;
		}
		if (table1.GetTableCheckConstraints().size() != table2.GetTableCheckConstraints().size())
			return false;
		return std::equal(
			table1.GetTableCheckConstraints().begin(),
			table1.GetTableCheckConstraints().end(),
			table2.GetTableCheckConstraints().begin());
	}

}  // namespace DbSchema
