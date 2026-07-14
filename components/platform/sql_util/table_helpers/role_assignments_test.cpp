#include "role_assignments.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "db_schema/roles.h"
#include "db_schema/role_assignments.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace TableHelpers {
namespace {

static int64_t AddRole(Transaction& transaction, TestDatabaseUtil& testDb, std::string_view name, std::string_view description) {
    KeyValueTable kv = {
        { std::string(DbSchema::kRolesName), std::string(name) },
        { std::string(DbSchema::kRolesDescription), std::string(description) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        testDb.GetDatabaseHelper(),
        DbSchema::kRolesTable,
        kv);
}

TEST(RoleAssignmentsTest, AddRoleAssignmentBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddRoleAssignmentBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        // Add person via helper util
        int64_t personId = testDb.AddPerson(transaction, "user@example.com", "First", "Last", "pwdhash");
        // Add role
        int64_t roleId = AddRole(transaction, testDb, "member", "Member role");

        int64_t id = ra.AddRoleAssignment(transaction, personId, roleId);
        ASSERT_GT(id, 0);

        KeyValueTable row = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM role_assignments WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kRoleAssignmentsPersonId)), StringFromInt(personId));
        EXPECT_EQ(row.at(std::string(DbSchema::kRoleAssignmentsRoleId)), StringFromInt(roleId));
    });
}

TEST(RoleAssignmentsTest, AddRoleAssignmentDuplicate) {
    // Phase 1.4 of the security review: (person_id, role_id) is unique.
    // Inserting the same pair twice must fail with a Postgres unique
    // violation, surfaced as an exception.
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddRoleAssignmentDuplicate", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId = testDb.AddPerson(
            transaction, "dup@example.com", "First", "Last", "pwdhash");
        int64_t roleId = AddRole(transaction, testDb, "dup-role", "Dup role");

        ASSERT_GT(ra.AddRoleAssignment(transaction, personId, roleId), 0);
        EXPECT_THROW(
            ra.AddRoleAssignment(transaction, personId, roleId),
            std::exception);
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId = testDb.AddPerson(transaction, "user2@example.com", "F", "L", "pwdhash");
        int64_t roleId = AddRole(transaction, testDb, "auditor", "Auditor");
        int64_t id = ra.AddRoleAssignment(transaction, personId, roleId);

        KeyValueTable row = ra.GetRoleAssignments(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kRoleAssignmentsPersonId)), StringFromInt(personId));
        EXPECT_EQ(row.at(std::string(DbSchema::kRoleAssignmentsRoleId)), StringFromInt(roleId));
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsNotFound", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        EXPECT_THROW(ra.GetRoleAssignments(transaction, 424242), std::runtime_error);
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForPersonBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForPersonBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId = testDb.AddPerson(transaction, "user3@example.com", "F", "L", "pwdhash");
        int64_t roleId1 = AddRole(transaction, testDb, "reader", "Reader");
        int64_t roleId2 = AddRole(transaction, testDb, "writer", "Writer");
        ra.AddRoleAssignment(transaction, personId, roleId1);
        ra.AddRoleAssignment(transaction, personId, roleId2);

        KeyValueTableArray arr = ra.GetRoleAssignmentsForPerson(transaction, personId);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRoleAssignmentsPersonId)), StringFromInt(personId));
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kRoleAssignmentsPersonId)), StringFromInt(personId));
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForPersonNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForPersonNotFound", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = ra.GetRoleAssignmentsForPerson(transaction, 999999);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForPersonNoAssignments) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForPersonNoAssignments", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        // Person exists, but has no role assignments
        int64_t personId = testDb.AddPerson(transaction, "noassign@example.com", "F", "L", "pwdhash");
        (void)AddRole(transaction, testDb, "member", "Member role");

        KeyValueTableArray arr = ra.GetRoleAssignmentsForPerson(transaction, personId);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForRoleBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForRoleBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId1 = testDb.AddPerson(transaction, "user4@example.com", "F", "L", "pwdhash");
        int64_t personId2 = testDb.AddPerson(transaction, "user5@example.com", "F", "L", "pwdhash");
        int64_t roleId = AddRole(transaction, testDb, "engineer", "Engineer");
        ra.AddRoleAssignment(transaction, personId1, roleId);
        ra.AddRoleAssignment(transaction, personId2, roleId);

        KeyValueTableArray arr = ra.GetRoleAssignmentsForRole(transaction, roleId);
        ASSERT_EQ(arr.size(), 2u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRoleAssignmentsRoleId)), StringFromInt(roleId));
        EXPECT_EQ(arr[1].at(std::string(DbSchema::kRoleAssignmentsRoleId)), StringFromInt(roleId));
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForRoleNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForRoleNotFound", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = ra.GetRoleAssignmentsForRole(transaction, 999999);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsForRoleNoAssignments) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsForRoleNoAssignments", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        // Role exists, but has no assignments
        int64_t personId = testDb.AddPerson(transaction, "role_noassign@example.com", "F", "L", "pwdhash");
        (void)personId;
        int64_t roleId = AddRole(transaction, testDb, "unassigned", "Unassigned role");

        KeyValueTableArray arr = ra.GetRoleAssignmentsForRole(transaction, roleId);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsAllBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsAllBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId = testDb.AddPerson(transaction, "user6@example.com", "F", "L", "pwdhash");
        int64_t roleId = AddRole(transaction, testDb, "operator", "Operator");
        ra.AddRoleAssignment(transaction, personId, roleId);

        KeyValueTableArray arr = ra.GetRoleAssignments(transaction);
        ASSERT_EQ(arr.size(), 1u);
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRoleAssignmentsPersonId)), StringFromInt(personId));
        EXPECT_EQ(arr[0].at(std::string(DbSchema::kRoleAssignmentsRoleId)), StringFromInt(roleId));
    });
}

TEST(RoleAssignmentsTest, GetRoleAssignmentsNoRoleAssignments) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetRoleAssignmentsNoRoleAssignments", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        KeyValueTableArray arr = ra.GetRoleAssignments(transaction);
        EXPECT_TRUE(arr.empty());
    });
}

TEST(RoleAssignmentsTest, DeleteRoleAssignmentBasic) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRoleAssignmentBasic", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        int64_t personId = testDb.AddPerson(transaction, "user7@example.com", "F", "L", "pwdhash");
        int64_t roleId = AddRole(transaction, testDb, "temp", "Temp role");
        int64_t id = ra.AddRoleAssignment(transaction, personId, roleId);

        // ensure exists
        KeyValueTable pre = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM role_assignments WHERE id = $1", StringFromInt(id));
        ASSERT_FALSE(pre.empty());

        ra.DeleteRoleAssignment(transaction, id);

        std::string countStr = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM role_assignments WHERE id = $1", StringFromInt(id));
        EXPECT_EQ(std::stoi(countStr), 0);
    });
}

TEST(RoleAssignmentsTest, DeleteRoleAssignmentNotFound) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteRoleAssignmentNotFound", [&](Transaction& transaction) {

        RoleAssignments ra(testDb.GetDatabaseHelper());

        // Should not throw
        EXPECT_NO_THROW(ra.DeleteRoleAssignment(transaction, 424242));
    });
}

} // namespace
} // namespace TableHelpers