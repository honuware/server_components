#include "config_secrets.h"

namespace DbSchema {

	void MakeConfigSecretsTable(DatabaseInfo databaseInfo) {
		databaseInfo.AddTable(kConfigSecretsTable);
		databaseInfo.AddColumnPrimaryKey(
			kConfigSecretsTable,
			kConfigSecretsId,
			DB_TYPE_BIGSERIAL);
		databaseInfo.AddColumnUnique(
			kConfigSecretsTable,
			kConfigSecretsName,
			DB_TYPE_STRING);
		databaseInfo.AddColumnSimple(
			kConfigSecretsTable,
			kConfigSecretsValue,
            DB_TYPE_STRING);
	}

}  // namespace DbSchema