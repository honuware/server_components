#include "admin_column_data_info.h"

#include "admin_top_level_tables.h"

namespace DbSchema {

	void MakeAdminColumnDataInfoTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminColumnDataInfoTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoColumnDataInfoId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kAdminTopLevelTablesTable,
			kAdminTopLevelTablesName,
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoTableName);
		databaseInfo.AddColumnSimple(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoColumnName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoLabel,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoHint,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoPlaceHolder,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoRegex,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoHtmlInputType,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoRequired,
			DB_TYPE_BOOL);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoMaxLength,
			DB_TYPE_INT);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoDefaultValue,
			DB_TYPE_STRING);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoRows,
			DB_TYPE_INT);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoHidden,
			DB_TYPE_BOOL);
		databaseInfo.AddColumnNullable(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoReadonly,
			DB_TYPE_BOOL);
	}

}  // namespace DbSchema