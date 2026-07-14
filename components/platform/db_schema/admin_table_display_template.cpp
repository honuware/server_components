#include "admin_table_display_template.h"

namespace DbSchema {

	void MakeAdminTableDisplayTemplateTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminTableDisplayTemplateTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminTableDisplayTemplateTable,
			kAdminTableDisplayTemplateName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminTableDisplayTemplateTable,
			kAdminTableDisplayTemplateTemplate,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema
