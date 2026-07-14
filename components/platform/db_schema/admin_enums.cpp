#include "admin_enums.h"

namespace DbSchema {

	void MakeAdminEnumsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminEnumsTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminEnumsTable,
			kAdminEnumsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnUnique(
			kAdminEnumsTable,
			kAdminEnumsName,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema
