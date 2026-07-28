#include "business_logic/create_framework_tables.h"

#include <string>

#include <gtest/gtest.h>

#include "db_schema/people.h"
#include "db_schema/permission_implications.h"
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/roles.h"
#include "test/src/util/database_test_helper.h"

// H2 extraction: framework-only stand-up. The honuware harness pre-creates the
// framework tables (MakeFrameworkTables), so this exercises PopulateFrameworkTables
// — the framework baseline seed a consuming app (e.g. CommunityFinder) composes
// with its own app seed. CreateFrameworkTables's DDL is exercised live by the
// app's --recreate_database (the recompose gate).
namespace {

TEST(CreateFrameworkTablesTest, PopulateFrameworkTablesSeedsBaseline) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "PopulateFrameworkTablesSeedsBaseline", [&](Transaction& transaction) {
            DbSchema::DatabaseInfo info("framework_test");
            PopulateFrameworkTables(transaction, testDb.GetDatabaseHelper(), info);

            // Base roles present.
            TableHelpers::Roles roles(testDb.GetDatabaseHelper());
            auto adminRole = roles.GetRole(transaction, DbSchema::kRoleNameAdmin);
            ASSERT_FALSE(adminRole.empty());
            EXPECT_FALSE(roles.GetRole(transaction, DbSchema::kRoleNameUser).empty());

            // Base framework permissions present.
            TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
            EXPECT_FALSE(perms.GetPermission(transaction, "admin_portal").empty());
            EXPECT_FALSE(
                perms.GetPermission(transaction, DbSchema::kPermissionStaffAccess)
                    .empty());

            // Administrator granted exactly the two framework permissions.
            int64_t adminRoleId =
                std::stoll(adminRole.at(std::string(DbSchema::kRolesId)));
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM role_permissions WHERE role_id = $1",
                    std::to_string(adminRoleId)),
                "2");

            // permission_implications is on the allow-list.
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM allowed_tables WHERE name = $1",
                    std::string(DbSchema::kPermissionImplicationsTable)),
                "1");

            // people has photo support.
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM photo_support_tables "
                    "WHERE table_name = $1",
                    std::string(DbSchema::kPeopleTable)),
                "1");

            // Security redactions present (people.password_hash + the other 3).
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM admin_column_redactions"),
                "4");
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM admin_column_redactions "
                    "WHERE table_name = $1 AND column_name = $2",
                    std::string(DbSchema::kPeopleTable),
                    std::string(DbSchema::kPeoplePasswordHash)),
                "1");

            // Framework admin metadata: people appears as a top-level table and a
            // friendly-named table.
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM admin_top_level_tables WHERE name = $1",
                    std::string(DbSchema::kPeopleTable)),
                "1");
            EXPECT_EQ(
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM admin_table_friendly_names "
                    "WHERE name = $1",
                    std::string(DbSchema::kPeopleTable)),
                "1");

            // Config-secret defaults were seeded.
            EXPECT_GT(
                std::stoll(transaction.RunSqlStatementReturningOneValue(
                    "SELECT count(*) FROM config_secrets")),
                0);
        });
}

// Regression: the people timestamp columns are seeded so the admin CRUD editor
// renders them well — email_verified_at / created_at / updated_at as `date` columns
// (so the client formats the microsecond value) with friendly list-view headers.
// email_verified_at was previously missed entirely, surfacing as a raw microsecond
// integer under a raw `email_verified_at` header.
TEST(CreateFrameworkTablesTest, PopulateFrameworkTablesSeedsTimestampDisplayMetadata) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "PopulateFrameworkTablesSeedsTimestampDisplayMetadata",
        [&](Transaction& transaction) {
            DbSchema::DatabaseInfo info("framework_test");
            PopulateFrameworkTables(transaction, testDb.GetDatabaseHelper(), info);

            auto htmlInputType = [&](std::string_view column) {
                return transaction.RunSqlStatementReturningOneValue(
                    "SELECT html_input_type FROM admin_column_data_info "
                    "WHERE table_name = $1 AND column_name = $2",
                    std::string(DbSchema::kPeopleTable), std::string(column));
            };
            EXPECT_EQ(htmlInputType(DbSchema::kPeopleEmailVerifiedAt), "date");
            EXPECT_EQ(htmlInputType(DbSchema::kPeopleCreatedAt), "date");
            EXPECT_EQ(htmlInputType(DbSchema::kPeopleUpdatedAt), "date");

            auto friendlyName = [&](std::string_view column) {
                return transaction.RunSqlStatementReturningOneValue(
                    "SELECT friendly_name FROM admin_column_friendly_names "
                    "WHERE table_name = $1 AND column_name = $2",
                    std::string(DbSchema::kPeopleTable), std::string(column));
            };
            EXPECT_EQ(friendlyName(DbSchema::kPeopleEmailVerifiedAt), "Email Verified");
            EXPECT_EQ(friendlyName(DbSchema::kPeopleCreatedAt), "Created");
            EXPECT_EQ(friendlyName(DbSchema::kPeopleUpdatedAt), "Updated");
        });
}

}  // namespace
