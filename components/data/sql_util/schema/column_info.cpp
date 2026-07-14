#include "column_info.h"

namespace DbSchema {

	ColumnInfo::ColumnInfo(
		std::string_view columnName,
		DatabaseTypes databaseType,
		bool primaryKey /* = false */,
		bool unique /* = false */,
		bool nullable /* = false */,
		std::string defaultValue /* = std::string()*/)
		: columnName_(columnName), databaseType_(databaseType),
		primaryKey_(primaryKey), unique_(unique), nullable_(nullable),
		default_(defaultValue) {}

	std::string ColumnInfo::GetSqlType() const {
		return SqlTypeFromDatabaseType(databaseType_);
	}

	std::string ColumnInfo::SqlTypeFromDatabaseType(
		DatabaseTypes databaseType) {
		switch (databaseType) {
			case DB_TYPE_INT:
				return "INT";
			case DB_TYPE_SERIAL:
				return "SERIAL";
			case DB_TYPE_BOOL:
				return "BOOL";
			case DB_TYPE_DOUBLE:
				return "FLOAT8";
			case DB_TYPE_STRING:
				return "VARCHAR";
			case DB_TYPE_JSON:
				return "JSON";
			case DB_TYPE_BYTES:
				return "BYTEA";
			case DB_TYPE_DATE:
				return "DATE";
			case DB_TYPE_TIME:
				return "TIME";
			case DB_TYPE_UUID:
				return "UUID";
			case DB_TYPE_NULL:
				return "NULL";
			case DB_TYPE_TIMESTAMP:
                return "TIMESTAMP";
			case DB_TYPE_TIMESTAMPTZ:
                return "TIMESTAMPTZ";
            case DB_TYPE_BIGINT:
                return "BIGINT";
            case DB_TYPE_BIGSERIAL:
                return "BIGSERIAL";
            case DB_TYPE_CITEXT:
                return "CITEXT";
			default:
			{
				std::stringstream ss;
				ss << "ColumnInfo::SqlTypeFromDatabaseType - invalid type: " << databaseType;
				throw std::invalid_argument(ss.str());
			}
		}
	}

}  // namespace DbSchema
