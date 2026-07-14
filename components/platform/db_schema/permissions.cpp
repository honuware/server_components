#include "permissions.h"

namespace DbSchema {

    void MakePermissionsTable(DatabaseInfo databaseInfo) {
        databaseInfo.AddTable(kPermissionsTable);
        databaseInfo.AddColumnPrimaryKey(
            kPermissionsTable,
            kPermissionsId,
            DB_TYPE_BIGSERIAL);
        databaseInfo.AddColumnSimple(
            kPermissionsTable,
            kPermissionsName,
            DB_TYPE_STRING);
        databaseInfo.AddColumnSimple(
            kPermissionsTable,
            kPermissionsDescription,
            DB_TYPE_STRING);
        databaseInfo.AddColumnNotNullableWithDefault(
            kPermissionsTable,
            kPermissionsIsPricingEligible,
            DB_TYPE_BOOL,
            kDatabaseInfoDefaultFalse);
    }

}  // namespace DbSchema