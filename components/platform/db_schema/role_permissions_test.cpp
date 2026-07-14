#include "role_permissions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "permissions.h"
#include "roles.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

using ::testing::ElementsAre;

TEST(RolePermissionsSchemaTest, MakeRolePermissionsTableRegistersCompositeUniqueConstraint) {
    DatabaseInfo info("knotty_yoga_test");
    MakeRolesTable(info);
    MakePermissionsTable(info);
    MakeRolePermissionsTable(info);

    const TableUniqueValuesArray& uniques =
        info.GetTableInfo(kRolePermissionsTable).GetTableUniqueValues();
    ASSERT_EQ(uniques.size(), 1u);
    EXPECT_THAT(uniques[0].columns,
        ElementsAre(
            std::string(kRolePermissionsRoleId),
            std::string(kRolePermissionsPermissionId)));
}

TEST(RolePermissionsSchemaTest, GenerateCreateTableSqlIncludesCompositeUnique) {
    DatabaseInfo info("knotty_yoga_test");
    MakeRolesTable(info);
    MakePermissionsTable(info);
    MakeRolePermissionsTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kRolePermissionsTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("UNIQUE (role_id, permission_id)"), std::string::npos)
        << "expected UNIQUE (role_id, permission_id) in DDL, got: " << sql;
}

}  // namespace
}  // namespace DbSchema
