#include "permission_implications.h"

#include "permissions.h"

namespace DbSchema {

	void MakePermissionImplicationsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kPermissionImplicationsTable);
		databaseInfo.AddColumnPrimaryKey(
			kPermissionImplicationsTable,
			kPermissionImplicationsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnForeignKeyRef(
			kPermissionsTable,
			kPermissionsId,
			kPermissionImplicationsTable,
			kPermissionImplicationsPermissionId);
		databaseInfo.AddColumnForeignKeyRef(
			kPermissionsTable,
			kPermissionsId,
			kPermissionImplicationsTable,
			kPermissionImplicationsImpliesPermissionId);
		// Each implication edge is meaningful at most once.
		databaseInfo.AddUniqueConstraint(
			kPermissionImplicationsTable,
			kPermissionImplicationsPermissionId,
			kPermissionImplicationsImpliesPermissionId);
	}

	void CreatePermissionImplicationsIndexes(Transaction& transaction) {
		transaction.RunSqlStatement(
			"CREATE INDEX IF NOT EXISTS permission_implications_permission_id_idx "
			"ON permission_implications (permission_id)");
	}

}  // namespace DbSchema
