#include "allowed_tables.h"

namespace DbSchema {

	void MakeAllowedTablesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAllowedTablesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAllowedTablesTable, 
			kAllowedTablesName, 
			DB_TYPE_STRING);
	}

}  // namespace DbSchema