#include "admin_top_level_tables.h"

namespace DbSchema {

	void MakeAdminTopLevelTablesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminTopLevelTablesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminTopLevelTablesTable,
			kAdminTopLevelTablesName,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema