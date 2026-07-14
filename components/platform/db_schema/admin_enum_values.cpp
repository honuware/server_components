#include "admin_enum_values.h"

#include "admin_enums.h"

namespace DbSchema {

	void MakeAdminEnumValuesTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminEnumValuesTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminEnumValuesTable,
			kAdminEnumValuesId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kAdminEnumsTable,
			kAdminEnumsId,
			kAdminEnumValuesTable,
			kAdminEnumValuesAdminEnumId);
		databaseInfo.AddColumnSimple(
			kAdminEnumValuesTable,
			kAdminEnumValuesName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminEnumValuesTable,
			kAdminEnumValuesValue,
			DB_TYPE_INT);
	}

}  // namespace DbSchema
