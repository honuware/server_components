#include "admin_column_friendly_names.h"

#include "admin_top_level_tables.h"

namespace DbSchema {

	void MakeAdminColumnFriendlyNamesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminColumnFriendlyNamesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminColumnFriendlyNamesTable,
			kAdminColumnFriendlyNamesColumnFriendlyNameId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kAdminTopLevelTablesTable,
			kAdminTopLevelTablesName,
			kAdminColumnFriendlyNamesTable,
			kAdminColumnFriendlyNamesTableName);
		databaseInfo.AddColumnSimple(
			kAdminColumnFriendlyNamesTable,
			kAdminColumnFriendlyNamesColumnName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminColumnFriendlyNamesTable,
			kAdminColumnFriendlyNamesFriendlyName,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema