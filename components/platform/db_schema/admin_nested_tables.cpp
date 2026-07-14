#include "admin_nested_tables.h"

namespace DbSchema {

	void MakeAdminNestedTablesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminNestedTablesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminNestedTablesTable,
			kAdminNestedTablesName,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema
