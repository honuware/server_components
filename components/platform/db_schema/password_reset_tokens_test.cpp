#include "password_reset_tokens.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "people.h"
#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

std::string CreateTableSql() {
    DatabaseInfo info("honuware_test");
    MakePeopleTable(info);
    MakePasswordResetTokensTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kPasswordResetTokensTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);
    return sql;
}

TEST(PasswordResetTokensSchemaTest, OnePendingLinkPerPerson) {
    DatabaseInfo info("honuware_test");
    MakePeopleTable(info);
    MakePasswordResetTokensTable(info);

    const TableUniqueValuesArray& uniques =
        info.GetTableInfo(kPasswordResetTokensTable).GetTableUniqueValues();
    ASSERT_EQ(uniques.size(), 1u);
    EXPECT_THAT(uniques[0].columns,
        ElementsAre(std::string(kPasswordResetTokensPersonId)));
    EXPECT_THAT(CreateTableSql(), HasSubstr("UNIQUE (person_id)"));
}

TEST(PasswordResetTokensSchemaTest, HasEveryColumnThePlanNames) {
    const std::string sql = CreateTableSql();
    for (std::string_view column : {
             kPasswordResetTokensId,
             kPasswordResetTokensPersonId,
             kPasswordResetTokensTokenHash,
             kPasswordResetTokensExpiresAt,
             kPasswordResetTokensAttempts,
             kPasswordResetTokensCreatedAt }) {
        EXPECT_THAT(sql, HasSubstr(std::string(column))) << column;
    }
}

TEST(PasswordResetTokensSchemaTest, PersonIdReferencesPeople) {
    EXPECT_THAT(CreateTableSql(), HasSubstr("REFERENCES people"));
}

}  // namespace
}  // namespace DbSchema
