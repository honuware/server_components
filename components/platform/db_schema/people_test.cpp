#include "people.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_access/db_and_table_operations.h"

namespace DbSchema {
namespace {

TEST(PeopleSchemaTest, EmailColumnIsCitext) {
    // Phase 1.6 of the security review: people.email lives as CITEXT so the
    // UNIQUE constraint and every lookup are case-insensitive at the DB
    // level.
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);

    const ColumnInfo& emailColumn =
        info.GetTableInfo(kPeopleTable).GetColumn(kPeopleEmail);
    EXPECT_EQ(emailColumn.GetDatabaseType(), DB_TYPE_CITEXT);
    EXPECT_TRUE(emailColumn.IsUnique());
    EXPECT_FALSE(emailColumn.IsNullable());
}

TEST(PeopleSchemaTest, GenerateCreateTableSqlEmitsCitextEmail) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kPeopleTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("email CITEXT UNIQUE"), std::string::npos)
        << "expected `email CITEXT UNIQUE` in DDL, got: " << sql;
}

TEST(PeopleSchemaTest, FailedLoginAttemptsColumnIsBigintWithZeroDefault) {
    // Phase 1.7 of the security review: failed_login_attempts is a BIGINT
    // counter that starts at 0 and is incremented on each failed login.
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);

    const ColumnInfo& column =
        info.GetTableInfo(kPeopleTable).GetColumn(kPeopleFailedLoginAttempts);
    EXPECT_EQ(column.GetDatabaseType(), DB_TYPE_BIGINT);
    EXPECT_FALSE(column.IsNullable());
    EXPECT_FALSE(column.IsUnique());
    std::string defaultValue;
    ASSERT_TRUE(column.IsDefault(defaultValue));
    EXPECT_EQ(defaultValue, kDatabaseInfoDefaultZero);
}

TEST(PeopleSchemaTest, LockedUntilColumnIsNullableBigint) {
    // Phase 1.7 of the security review: locked_until is microseconds since
    // epoch, NULL meaning the account is not currently locked.
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);

    const ColumnInfo& column =
        info.GetTableInfo(kPeopleTable).GetColumn(kPeopleLockedUntil);
    EXPECT_EQ(column.GetDatabaseType(), DB_TYPE_BIGINT);
    EXPECT_TRUE(column.IsNullable());
    EXPECT_FALSE(column.IsUnique());
    std::string defaultValue;
    EXPECT_FALSE(column.IsDefault(defaultValue));
}

TEST(PeopleSchemaTest, GenerateCreateTableSqlEmitsLockoutColumns) {
    DatabaseInfo info("knotty_yoga_test");
    MakePeopleTable(info);

    bool wasIfNotExists = DbOps::IsIfNotExistsMode();
    DbOps::SetIfNotExistsMode(false);
    std::string sql = DbOps::GenerateCreateTableSql(info, kPeopleTable);
    DbOps::SetIfNotExistsMode(wasIfNotExists);

    EXPECT_NE(sql.find("failed_login_attempts BIGINT NOT NULL DEFAULT 0"),
        std::string::npos)
        << "expected failed_login_attempts BIGINT NOT NULL DEFAULT 0 in DDL, got: "
        << sql;
    // Nullable BIGINT — no NOT NULL, no DEFAULT.
    EXPECT_NE(sql.find("locked_until BIGINT"), std::string::npos)
        << "expected locked_until BIGINT in DDL, got: " << sql;
    EXPECT_EQ(sql.find("locked_until BIGINT NOT NULL"), std::string::npos)
        << "locked_until should be nullable, got: " << sql;
}

}  // namespace
}  // namespace DbSchema
