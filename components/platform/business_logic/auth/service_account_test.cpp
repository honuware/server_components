#include "service_account.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/auth_helper.h"
#include "business_logic/auth/person.h"
#include "db_schema/people.h"
#include "db_schema/permissions.h"
#include "db_schema/roles.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/roles.h"
#include "test/src/util/database_test_helper.h"
#include "util/types.h"

namespace Auth {
namespace {

// Phase 3.2: the service-account domain + scheduler email are app-supplied
// (App::kServiceAccountEmailDomain / App::kSchedulerServiceAccountEmail). The
// framework carries no brand, so this framework test uses its own neutral test
// identity and passes it into the (now parameterized) functions.
constexpr std::string_view kTestServiceAccountDomain = "@svc.test";
constexpr std::string_view kTestSchedulerEmail = "scheduler@svc.test";

// Cross-platform setenv/unsetenv (mirrors the helpers used by health_test
// and scheduler_config_test).
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// RAII guard that restores SCHEDULER_SERVICE_ACCOUNT_PASSWORD on exit so
// individual tests can mutate it freely.
class PasswordEnvScope {
public:
    PasswordEnvScope() {
        const char* val = std::getenv(
            std::string(kSchedulerServiceAccountPasswordEnvVar).c_str());
        had_ = (val != nullptr);
        if (had_) prior_.assign(val);
        UnsetEnv(std::string(kSchedulerServiceAccountPasswordEnvVar).c_str());
    }
    ~PasswordEnvScope() {
        if (had_) {
            SetEnv(std::string(kSchedulerServiceAccountPasswordEnvVar).c_str(),
                   prior_.c_str());
        } else {
            UnsetEnv(
                std::string(kSchedulerServiceAccountPasswordEnvVar).c_str());
        }
    }
private:
    bool had_ = false;
    std::string prior_;
};

// App permissions a test can ask for by name.
constexpr std::string_view kTestRunJobsPermission = "run_test_jobs";
constexpr std::string_view kTestReadReportsPermission = "read_test_reports";

int64_t SeedPermission(
    Transaction& transaction, DatabaseHelper databaseHelper,
    std::string_view name) {
    TableHelpers::Permissions permissions(databaseHelper);
    return permissions.AddPermission(transaction, name, "A test permission.");
}

// EnsureSchedulerServiceAccount always grants run_scheduled_jobs, which
// PopulateFrameworkTables seeds in production. The harness doesn't run the
// seed, so tests add the row up front.
int64_t SeedRunScheduledJobs(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    return SeedPermission(
        transaction, databaseHelper, DbSchema::kPermissionRunScheduledJobs);
}

// The scheduler person's permission names, sorted.
StringArray SchedulerPermissions(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    TableHelpers::People people(databaseHelper);
    int64_t personId = std::stoll(
        people.LookupPersonByEmail(transaction, kTestSchedulerEmail)
            .at(std::string(DbSchema::kPeopleId)));
    PersonHelper personHelper(databaseHelper);
    StringArray perms = personHelper.GetPermissionsForUser(
        transaction, personId);
    std::sort(perms.begin(), perms.end());
    return perms;
}

// --- IsServiceAccountEmail ---

TEST(ServiceAccountTest, IsServiceAccountEmailRecognizesDomain) {
    EXPECT_TRUE(IsServiceAccountEmail(
        "scheduler@svc.test", kTestServiceAccountDomain));
    EXPECT_TRUE(IsServiceAccountEmail(
        "anything@svc.test", kTestServiceAccountDomain));
}

TEST(ServiceAccountTest, IsServiceAccountEmailRejectsRealDomains) {
    EXPECT_FALSE(IsServiceAccountEmail("user@svc.com", kTestServiceAccountDomain));
    EXPECT_FALSE(IsServiceAccountEmail("user@example.com", kTestServiceAccountDomain));
    EXPECT_FALSE(IsServiceAccountEmail("svc.test", kTestServiceAccountDomain));  // no @
    EXPECT_FALSE(IsServiceAccountEmail("", kTestServiceAccountDomain));
}

TEST(ServiceAccountTest, IsServiceAccountEmailIsSubstringSafe) {
    // Domain match must be at the END of the email, not anywhere.
    EXPECT_FALSE(IsServiceAccountEmail(
        "foo@svc.test.example.com", kTestServiceAccountDomain));
}

// --- ReadSchedulerServiceAccountPassword ---

TEST(ServiceAccountTest, ReadPasswordReturnsEnvValue) {
    PasswordEnvScope guard;
    SetEnv(std::string(kSchedulerServiceAccountPasswordEnvVar).c_str(),
           "supersecret");
    EXPECT_EQ(ReadSchedulerServiceAccountPassword(), "supersecret");
}

TEST(ServiceAccountTest, ReadPasswordThrowsWhenUnset) {
    PasswordEnvScope guard;
    EXPECT_THROW((void)ReadSchedulerServiceAccountPassword(),
                 std::runtime_error);
}

TEST(ServiceAccountTest, ReadPasswordThrowsWhenEmpty) {
    PasswordEnvScope guard;
    SetEnv(std::string(kSchedulerServiceAccountPasswordEnvVar).c_str(), "");
    EXPECT_THROW((void)ReadSchedulerServiceAccountPassword(),
                 std::runtime_error);
}

// --- EnsureSchedulerServiceAccount ---

TEST(ServiceAccountTest, EnsureCreatesPersonWithExpectedFields) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureCreatesPersonWithExpectedFields",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "secret", {});

        TableHelpers::People people(databaseHelper);
        KeyValueTable row = people.LookupPersonByEmail(
            transaction, kTestSchedulerEmail);
        ASSERT_FALSE(row.empty());
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleEmail)),
                  std::string(kTestSchedulerEmail));
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFirstName)),
                  std::string(kSchedulerServiceAccountFirstName));
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleLastName)),
                  std::string(kSchedulerServiceAccountLastName));

        // CreateFullyValidatedUser sets the email_verified_at column; the
        // generic IsPerson check returns true once that's set.
        PersonHelper personHelper(databaseHelper);
        EXPECT_TRUE(personHelper.IsPerson(
            transaction, kTestSchedulerEmail));

        // Password is hashed and verifies via VerifyPassword.
        EXPECT_TRUE(personHelper.VerifyPassword(
            transaction, kTestSchedulerEmail, "secret"));
    });
}

// No app permissions: the role holds run_scheduled_jobs alone, which is all
// the framework's job endpoints check.
TEST(ServiceAccountTest, EnsureWithNoAppPermissionsGrantsOnlyRunScheduledJobs) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureWithNoAppPermissionsGrantsOnlyRunScheduledJobs",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);
        // Present but not named, so not granted.
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "secret", {});

        EXPECT_EQ(SchedulerPermissions(transaction, databaseHelper),
                  StringArray({std::string(DbSchema::kPermissionRunScheduledJobs)}));
        TableHelpers::Roles roles(databaseHelper);
        EXPECT_EQ(roles.GetRole(transaction, kSchedulerRoleName)
                      .at(std::string(DbSchema::kRolesDescription)),
                  std::string(kSchedulerRoleDescription));
        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
                      "SELECT r.name FROM role_assignments ra "
                      "JOIN roles r ON r.id = ra.role_id "
                      "JOIN people p ON p.id = ra.person_id WHERE p.email = $1",
                      std::string(kTestSchedulerEmail)),
                  std::string(kSchedulerRoleName));
    });
}

TEST(ServiceAccountTest, EnsureWithAnAppPermissionGrantsItAlongsideRunScheduledJobs) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureWithAnAppPermission",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);
        SeedPermission(transaction, databaseHelper, kTestReadReportsPermission);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "secret",
            {kTestRunJobsPermission});

        EXPECT_EQ(SchedulerPermissions(transaction, databaseHelper),
                  StringArray({std::string(DbSchema::kPermissionRunScheduledJobs),
                               std::string(kTestRunJobsPermission)}));
    });
}

TEST(ServiceAccountTest, EnsureWithTwoAppPermissionsGrantsBoth) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureWithTwoAppPermissionsGrantsBoth",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);
        SeedPermission(transaction, databaseHelper, kTestReadReportsPermission);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "secret",
            {kTestRunJobsPermission, kTestReadReportsPermission});

        EXPECT_EQ(SchedulerPermissions(transaction, databaseHelper),
                  StringArray({std::string(kTestReadReportsPermission),
                               std::string(DbSchema::kPermissionRunScheduledJobs),
                               std::string(kTestRunJobsPermission)}));
    });
}

// Two calls back to back: one row, the first password, the same grants.
// Operators rotate by deleting the row first.
TEST(ServiceAccountTest, EnsureIsIdempotentAndKeepsTheFirstPassword) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureIsIdempotentAndKeepsTheFirstPassword",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "first",
            {kTestRunJobsPermission});
        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "second",
            {kTestRunJobsPermission});

        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
                      "SELECT COUNT(*) FROM people WHERE email = $1",
                      std::string(kTestSchedulerEmail)),
                  "1");
        PersonHelper personHelper(databaseHelper);
        EXPECT_TRUE(personHelper.VerifyPassword(
            transaction, kTestSchedulerEmail, "first"));
        EXPECT_FALSE(personHelper.VerifyPassword(
            transaction, kTestSchedulerEmail, "second"));
        EXPECT_EQ(SchedulerPermissions(transaction, databaseHelper),
                  StringArray({std::string(DbSchema::kPermissionRunScheduledJobs),
                               std::string(kTestRunJobsPermission)}));
    });
}

// The framework seed hasn't run: the throw comes before any write, so no
// person and no role are left behind, and the transaction is still usable.
TEST(ServiceAccountTest, EnsureWithoutRunScheduledJobsThrowsAndWritesNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureWithoutRunScheduledJobsThrows",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);

        EXPECT_THROW(
            EnsureSchedulerServiceAccount(
                transaction, databaseHelper, kTestSchedulerEmail, "secret",
                {kTestRunJobsPermission}),
            std::runtime_error);

        TableHelpers::People people(databaseHelper);
        EXPECT_TRUE(people.LookupPersonByEmail(
            transaction, kTestSchedulerEmail).empty());
        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
                      "SELECT COUNT(*) FROM roles WHERE name = $1",
                      std::string(kSchedulerRoleName)),
                  "0");
    });
}

// The same for a missing app permission.
TEST(ServiceAccountTest, EnsureWithAMissingAppPermissionThrowsAndWritesNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureWithAMissingAppPermissionThrows",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedRunScheduledJobs(transaction, databaseHelper);
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);

        EXPECT_THROW(
            EnsureSchedulerServiceAccount(
                transaction, databaseHelper, kTestSchedulerEmail, "secret",
                {kTestRunJobsPermission, kTestReadReportsPermission}),
            std::runtime_error);

        TableHelpers::People people(databaseHelper);
        EXPECT_TRUE(people.LookupPersonByEmail(
            transaction, kTestSchedulerEmail).empty());
        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
                      "SELECT COUNT(*) FROM roles WHERE name = $1",
                      std::string(kSchedulerRoleName)),
                  "0");
    });
}

// A rotation (delete the person, re-run) finds the role already there with
// its links; they aren't added twice, and a repeated name links once, even
// run_scheduled_jobs named again by the app.
TEST(ServiceAccountTest, EnsureReusesAnExistingRoleWithoutDuplicateLinks) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureReusesAnExistingRoleWithoutDuplicateLinks",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        int64_t runScheduledJobsId = SeedRunScheduledJobs(
            transaction, databaseHelper);
        SeedPermission(transaction, databaseHelper, kTestRunJobsPermission);
        TableHelpers::Roles roles(databaseHelper);
        int64_t roleId = roles.AddRole(
            transaction, kSchedulerRoleName, "Left from an earlier seed.");
        TableHelpers::RolePermissions rolePermissions(databaseHelper);
        rolePermissions.AddRolePermission(
            transaction, roleId, runScheduledJobsId);

        EnsureSchedulerServiceAccount(
            transaction, databaseHelper, kTestSchedulerEmail, "secret",
            {kTestRunJobsPermission, DbSchema::kPermissionRunScheduledJobs,
             kTestRunJobsPermission});

        EXPECT_EQ(rolePermissions.GetRolePermissionsForRole(
                      transaction, roleId).size(),
                  2u);
        EXPECT_EQ(SchedulerPermissions(transaction, databaseHelper),
                  StringArray({std::string(DbSchema::kPermissionRunScheduledJobs),
                               std::string(kTestRunJobsPermission)}));
    });
}

}  // namespace
}  // namespace Auth
