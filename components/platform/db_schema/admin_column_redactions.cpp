#include "admin_column_redactions.h"

namespace DbSchema {

	void MakeAdminColumnRedactionsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kAdminColumnRedactionsTable);
		databaseInfo.AddColumnPrimaryKey(
			kAdminColumnRedactionsTable,
			kAdminColumnRedactionsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnSimple(
			kAdminColumnRedactionsTable,
			kAdminColumnRedactionsTableName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kAdminColumnRedactionsTable,
			kAdminColumnRedactionsColumnName,
			DB_TYPE_STRING);
	}

}  // namespace DbSchema
