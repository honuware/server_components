#include "email_verifications.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "people.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

using ::testing::ElementsAre;

TEST(EmailVerificationsSchemaTest, MakeEmailVerificationsTableRegistersPersonIdUniqueConstraint) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeEmailVerificationsTable(info);

    const TableUniqueValuesArray& uniques =
        info.GetTableInfo(kEmailVerificationsTable).GetTableUniqueValues();
    ASSERT_EQ(uniques.size(), 1u);
    EXPECT_THAT(uniques[0].columns,
        ElementsAre(std::string(kEmailVerificationsPersonId)));
}

TEST(EmailVerificationsSchemaTest, GenerateCreateTableSqlIncludesPersonIdUnique) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);
    MakeEmailVerificationsTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kEmailVerificationsTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("UNIQUE (person_id)"), std::string::npos)
        << "expected UNIQUE (person_id) in DDL, got: " << sql;
}

}  // namespace
}  // namespace DbSchema
