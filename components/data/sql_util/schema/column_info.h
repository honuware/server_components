#pragma once

#include <string_view>

#include "sql_util/database_common.h"
#include "foreign_key_manager.h"

namespace DbSchema {

	class ColumnInfo {
	public:
		ColumnInfo(
			std::string_view columnName,
			DatabaseTypes databaseType,
			bool primaryKey = false,
			bool unique = false,
			bool nullable = false,
			std::string defaultValue = std::string());
		ColumnInfo() = default;
		ColumnInfo(const ColumnInfo&) = default;
		ColumnInfo(ColumnInfo&&) = default;
		ColumnInfo& operator=(const ColumnInfo&) = default;
		ColumnInfo& operator=(ColumnInfo&&) = default;
		~ColumnInfo() = default;

		const std::string& GetColumnName() const { return columnName_; }
		DatabaseTypes GetDatabaseType() const { return databaseType_; }
		bool IsPrimaryKey() const { return primaryKey_; }
		bool IsUnique() const { return unique_; }
		bool IsNullable() const { return nullable_; }
		std::string GetSqlType() const;
		bool IsDefault(std::string& defaultValue) const {
			defaultValue = default_;
            return !default_.empty();
		}

		static std::string SqlTypeFromDatabaseType(DatabaseTypes databaseType);

	private:
		std::string columnName_;
		DatabaseTypes databaseType_;
		bool primaryKey_ = false;
		bool unique_ = false;
		bool nullable_ = false;
        std::string default_;
	};

	inline bool operator==(const ColumnInfo& info1, const ColumnInfo& info2) {
        std::string defaultValue1, defaultValue2;
		bool defaultEqual = (info1.IsDefault(defaultValue1) == info2.IsDefault(defaultValue2));
        if (!defaultEqual) return false;
        if (defaultValue1 != defaultValue2) return false;
		return info1.GetColumnName() == info2.GetColumnName() &&
			info1.GetDatabaseType() == info2.GetDatabaseType() &&
			info1.IsPrimaryKey() == info2.IsPrimaryKey() &&
			info1.IsUnique() == info2.IsUnique();
	}

}  // namespace DbSchema
