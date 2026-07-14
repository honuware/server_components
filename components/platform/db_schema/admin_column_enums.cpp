#include "admin_column_enums.h"

#include "admin_enums.h"
#include "admin_column_data_info.h"

namespace DbSchema {

	void MakeAdminColumnEnumsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminColumnEnumsTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminColumnEnumsTable,
			kAdminColumnEnumsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kAdminEnumsTable,
			kAdminEnumsId,
			kAdminColumnEnumsTable,
			kAdminColumnEnumsAdminEnumId);
		databaseInfo.AddColumnForeignKeyRef(
			kAdminColumnDataInfoTable,
			kAdminColumnDataInfoColumnDataInfoId,
			kAdminColumnEnumsTable,
			kAdminColumnEnumsAdminColumnDataInfoId);
	}

}  // namespace DbSchema
