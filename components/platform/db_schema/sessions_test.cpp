#include "sessions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "people.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

using ::testing::ElementsAre;

TEST(SessionsSchemaTest, MakeSessionsTableRegistersUuidUniqueConstraint) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeSessionsTable(info);

    const TableUniqueValuesArray& uniques =
        info.GetTableInfo(kSessionsTable).GetTableUniqueValues();
    ASSERT_EQ(uniques.size(), 1u);
    EXPECT_THAT(uniques[0].columns, ElementsAre(std::string(kSessionsUuid)));
}

TEST(SessionsSchemaTest, GenerateCreateTableSqlIncludesUuidUnique) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeSessionsTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kSessionsTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("UNIQUE (uuid)"), std::string::npos)
        << "expected UNIQUE (uuid) in DDL, got: " << sql;
}

}  // namespace
}  // namespace DbSchema
