#include "admin_table_friendly_names.h"

namespace DbSchema {

	void MakeAdminTableFriendlyNamesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminTableFriendlyNamesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminTableFriendlyNamesTable,
			kAdminTableFriendlyNamesTableFriendlyNameId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnUnique(
			kAdminTableFriendlyNamesTable,
			kAdminTableFriendlyNamesName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminTableFriendlyNamesTable,
			kAdminTableFriendlyNamesFriendlyName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminTableFriendlyNamesTable,
			kAdminTableFriendlyNamesDescription,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema