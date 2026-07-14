#include "roles.h"

namespace DbSchema {

    void MakeRolesTable(DatabaseInfo databaseInfo) {
        databaseInfo.AddTable(kRolesTable);
        databaseInfo.AddColumnPrimaryKey(
            kRolesTable,
            kRolesId,
            DB_TYPE_BIGSERIAL);
        databaseInfo.AddColumnSimple(
            kRolesTable,
            kRolesName,
            DB_TYPE_STRING);
        databaseInfo.AddColumnSimple(
            kRolesTable,
            kRolesDescription,
            DB_TYPE_STRING);
    }

}  // namespace DbSchema