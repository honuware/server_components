#include "service_account.h"

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

// EnsureSchedulerServiceAccount expects the manage_subscriptions permission
// to already exist (PopulatePermissions does this in production). Tests
// mirror that prerequisite by inserting the row up front.
int64_t SeedManageSubscriptionsPermission(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    TableHelpers::Permissions permissions(databaseHelper);
    return permissions.AddPermission(
        transaction, kManageSubscriptionsPermission,
        "Permission to manage subscriptions, billing, and grace periods.");
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
        SeedManageSubscriptionsPermission(transaction, databaseHelper);

        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret");

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

TEST(ServiceAccountTest, EnsureGrantsManageSubscriptionsViaSchedulerRole) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureGrantsManageSubscriptionsViaSchedulerRole",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedManageSubscriptionsPermission(transaction, databaseHelper);

        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret");

        // The "scheduler" role exists and links to manage_subscriptions.
        TableHelpers::Roles roles(databaseHelper);
        KeyValueTable roleRow = roles.GetRole(transaction, kSchedulerRoleName);
        ASSERT_FALSE(roleRow.empty());

        // The scheduler person has the manage_subscriptions permission.
        TableHelpers::People people(databaseHelper);
        int64_t personId = std::stoll(
            people.LookupPersonByEmail(
                transaction, kTestSchedulerEmail)
                .at(std::string(DbSchema::kPeopleId)));

        PersonHelper personHelper(databaseHelper);
        StringArray perms = personHelper.GetPermissionsForUser(
            transaction, personId);
        bool hasManageSubscriptions = false;
        for (const auto& p : perms) {
            if (p == kManageSubscriptionsPermission) {
                hasManageSubscriptions = true;
                break;
            }
        }
        EXPECT_TRUE(hasManageSubscriptions)
            << "scheduler service account should have manage_subscriptions";
    });
}

// Classes Phase 11 §6: the scheduler runs the class crons, which gate on
// manage_class_schedule — Ensure grants it when the permission is seeded.
TEST(ServiceAccountTest, EnsureGrantsManageClassScheduleWhenSeeded) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureGrantsManageClassSchedule",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedManageSubscriptionsPermission(transaction, databaseHelper);
        TableHelpers::Permissions permissions(databaseHelper);
        permissions.AddPermission(
            transaction, DbSchema::kPermissionManageClassSchedule,
            "Permission to manage class schedules.");

        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret");

        TableHelpers::People people(databaseHelper);
        int64_t personId = std::stoll(
            people.LookupPersonByEmail(
                transaction, kTestSchedulerEmail)
                .at(std::string(DbSchema::kPeopleId)));
        PersonHelper personHelper(databaseHelper);
        StringArray perms = personHelper.GetPermissionsForUser(
            transaction, personId);

        bool hasManageClassSchedule = false;
        bool hasManageSubscriptions = false;
        for (const auto& p : perms) {
            if (p == DbSchema::kPermissionManageClassSchedule) {
                hasManageClassSchedule = true;
            }
            if (p == kManageSubscriptionsPermission) {
                hasManageSubscriptions = true;
            }
        }
        EXPECT_TRUE(hasManageClassSchedule)
            << "scheduler should hold manage_class_schedule for the class crons";
        // The original manage_subscriptions grant is unaffected.
        EXPECT_TRUE(hasManageSubscriptions);
    });
}

// Ensure still succeeds (and grants manage_subscriptions) when
// manage_class_schedule isn't seeded — the grant is best-effort so the auth
// unit tests that seed only manage_subscriptions are unaffected.
TEST(ServiceAccountTest, EnsureSucceedsWithoutManageClassScheduleSeeded) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureNoClassScheduleSeed",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedManageSubscriptionsPermission(transaction, databaseHelper);

        EXPECT_NO_THROW(
            EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret"));

        TableHelpers::People people(databaseHelper);
        int64_t personId = std::stoll(
            people.LookupPersonByEmail(
                transaction, kTestSchedulerEmail)
                .at(std::string(DbSchema::kPeopleId)));
        PersonHelper personHelper(databaseHelper);
        StringArray perms = personHelper.GetPermissionsForUser(
            transaction, personId);
        bool hasManageSubscriptions = false;
        for (const auto& p : perms) {
            if (p == kManageSubscriptionsPermission) hasManageSubscriptions = true;
        }
        EXPECT_TRUE(hasManageSubscriptions);
    });
}

TEST(ServiceAccountTest, EnsureIsIdempotent) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureIsIdempotent",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedManageSubscriptionsPermission(transaction, databaseHelper);

        // Two calls back-to-back must not throw and must not duplicate the
        // row.
        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret");
        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret");

        // Exactly one row with the service-account email exists.
        std::string count = transaction.RunSqlStatementReturningOneValue(
            "SELECT COUNT(*) FROM people WHERE email = $1",
            std::string(kTestSchedulerEmail));
        EXPECT_EQ(count, "1");
    });
}

TEST(ServiceAccountTest, EnsureSecondCallDoesNotOverwritePassword) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureSecondCallDoesNotOverwritePassword",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        SeedManageSubscriptionsPermission(transaction, databaseHelper);

        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "first");
        // Idempotent: second call with a different password is a no-op.
        // Operators rotate by deleting the row first.
        EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "second");

        PersonHelper personHelper(databaseHelper);
        EXPECT_TRUE(personHelper.VerifyPassword(
            transaction, kTestSchedulerEmail, "first"));
        EXPECT_FALSE(personHelper.VerifyPassword(
            transaction, kTestSchedulerEmail, "second"));
    });
}

TEST(ServiceAccountTest, EnsureThrowsWhenManageSubscriptionsMissing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("EnsureThrowsWhenManageSubscriptionsMissing",
        [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        // Intentionally do NOT seed the permission row.

        EXPECT_THROW(
            EnsureSchedulerServiceAccount(transaction, databaseHelper, kTestSchedulerEmail, "secret"),
            std::runtime_error);
    });
}

}  // namespace
}  // namespace Auth
