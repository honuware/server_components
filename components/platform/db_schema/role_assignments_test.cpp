#include "role_assignments.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "people.h"
#include "roles.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

using ::testing::ElementsAre;

TEST(RoleAssignmentsSchemaTest, MakeRoleAssignmentsTableRegistersCompositeUniqueConstraint) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeRolesTable(info);
    MakeRoleAssignmentsTable(info);

    const TableUniqueValuesArray& uniques =
        info.GetTableInfo(kRoleAssignmentsTable).GetTableUniqueValues();
    ASSERT_EQ(uniques.size(), 1u);
    EXPECT_THAT(uniques[0].columns,
        ElementsAre(
            std::string(kRoleAssignmentsPersonId),
            std::string(kRoleAssignmentsRoleId)));
}

TEST(RoleAssignmentsSchemaTest, GenerateCreateTableSqlIncludesCompositeUnique) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeRolesTable(info);
    MakeRoleAssignmentsTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kRoleAssignmentsTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("UNIQUE (person_id, role_id)"), std::string::npos)
        << "expected UNIQUE (person_id, role_id) in DDL, got: " << sql;
}

}  // namespace
}  // namespace DbSchema
