#include "admin_alerts.h"

namespace DbSchema {

void MakeAdminAlertsTable(DatabaseInfo databaseInfo) {
	databaseInfo.AddTable(kAdminAlertsTable);
	databaseInfo.AddColumnPrimaryKey(
		kAdminAlertsTable,
		kAdminAlertsId,
		DB_TYPE_BIGSERIAL);
	databaseInfo.AddColumnNotNullableWithDefault(
		kAdminAlertsTable,
		kAdminAlertsCreatedAt,
		DB_TYPE_BIGINT,
		kDatabaseInfoDefaultNow);
	databaseInfo.AddColumnSimple(
		kAdminAlertsTable,
		kAdminAlertsDescription,
		DB_TYPE_STRING);
}

}  // namespace DbSchema