#include "sql_util/table_helpers/people.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "db_schema/people.h"
#include "util/date_time_util.h"

#include <regex>

namespace TableHelpers {
namespace {

using ::testing::HasSubstr;

// AddPerson
TEST(PeopleTableTest, AddPersonBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddPersonBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "person1@hotmail.com", "first1", "last1", "hash1");
        ASSERT_GT(id1, 0);

        KeyValueTable keyValueTable = people.GetPersonById(transaction, id1);
        KeyValueTable expected = testDatabaseUtil.PersonKeyValueTable(
            id1, "person1@hotmail.com", "first1", "last1", "hash1");
        ASSERT_TRUE(CompareKeyValueTablesMinusColumns(
            keyValueTable,
            expected,
            std::string(DbSchema::kPeopleCreatedAt),
            std::string(DbSchema::kPeopleUpdatedAt),
            std::string(DbSchema::kPeopleEmailVerifiedAt),
            std::string(DbSchema::kPeopleMustChangePassword),
            std::string(DbSchema::kPeopleFailedLoginAttempts),
            std::string(DbSchema::kPeopleLockedUntil)));

        // Verify created_at and updated_at exist and are the same on insert; verified_at is null.
        int64_t createdAtUs = std::stoll(keyValueTable[std::string(DbSchema::kPeopleCreatedAt)]);
        int64_t updatedAtUs = std::stoll(keyValueTable[std::string(DbSchema::kPeopleUpdatedAt)]);
        int64_t tenMillisInUs = 10LL * 1000LL;
        EXPECT_TRUE(updatedAtUs - createdAtUs < tenMillisInUs) << "created_at and updated_at should be identical at insert";
        EXPECT_TRUE( keyValueTable[std::string(DbSchema::kPeopleEmailVerifiedAt)].empty()) << "verified_at should be null/empty at insert";
        });
}

TEST(PeopleTableTest, AddPersonDuplicateEmail) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddPersonDuplicateEmail", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        people.AddPerson(transaction, "dup@hotmail.com", "first1", "last1", "hash1");

        try {
            people.AddPerson(transaction, "dup@hotmail.com", "first2", "last2", "hash2");
            FAIL() << "Expected exception on duplicate email insert";
        } catch (const std::exception& e) {
            // DB-specific text varies; check common terms
            std::string msg = e.what();
            EXPECT_TRUE(msg.find("duplicate") != std::string::npos ||
                        msg.find("unique") != std::string::npos ||
                        msg.find("constraint") != std::string::npos)
                << "Unexpected error text: " << msg;
        }
        });
}

TEST(PeopleTableTest, AddPersonInitializesLockoutColumns) {
    // Phase 1.7 of the security review: every newly registered person starts
    // unlocked with no failed-login history. The defaults are owned by the
    // schema (DEFAULT 0 / NULL) — this asserts the helper relies on them
    // and doesn't accidentally write something else.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddPersonInitializesLockoutColumns",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int64_t id = people.AddPerson(
            transaction, "lockout-init@example.com", "Lock", "Init", "hash");
        ASSERT_GT(id, 0);

        KeyValueTable row = people.GetPersonById(transaction, id);
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleFailedLoginAttempts)),
            "0");
        // Nullable column comes back as an empty string when NULL.
        EXPECT_EQ(row.at(std::string(DbSchema::kPeopleLockedUntil)),
            std::string());
        });
}

TEST(PeopleTableTest, AddPersonEmailCaseInsensitive) {
    // Phase 1.6 of the security review: people.email is CITEXT so the unique
    // constraint and every lookup ignore case. Both case-folded and
    // upper-cased lookups must find the same row, and a second insert with
    // mixed case must fail on the unique constraint.
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("AddPersonEmailCaseInsensitive",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int64_t id = people.AddPerson(
            transaction, "Mason@Example.com", "Mason", "B", "hash");
        ASSERT_GT(id, 0);

        // Same email, lower-cased: must hit the same row.
        KeyValueTable lower = people.LookupPersonByEmail(
            transaction, "mason@example.com");
        ASSERT_FALSE(lower.empty());
        EXPECT_EQ(lower.at(std::string(DbSchema::kPeopleId)),
            std::to_string(id));

        // Same email, upper-cased: also same row.
        KeyValueTable upper = people.LookupPersonByEmail(
            transaction, "MASON@EXAMPLE.COM");
        ASSERT_FALSE(upper.empty());
        EXPECT_EQ(upper.at(std::string(DbSchema::kPeopleId)),
            std::to_string(id));

        // CITEXT preserves the original case in storage; only comparisons
        // fold case.
        EXPECT_EQ(lower.at(std::string(DbSchema::kPeopleEmail)),
            "Mason@Example.com");

        // A second insert with a differently-cased version of the same
        // email must violate the unique constraint.
        try {
            people.AddPerson(
                transaction, "mason@example.com", "Other", "Name", "hash2");
            FAIL() << "expected unique-violation on case-folded duplicate insert";
        } catch (const std::exception& e) {
            std::string msg = e.what();
            EXPECT_TRUE(msg.find("duplicate") != std::string::npos ||
                        msg.find("unique") != std::string::npos ||
                        msg.find("constraint") != std::string::npos)
                << "unexpected error text: " << msg;
        }
        });
}

// GetPeople
TEST(PeopleTableTest, GetPeopleBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetPeopleBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(transaction, "p1@hotmail.com", "first1", "last1", "hash1");
        int id2 = people.AddPerson(transaction, "p2@hotmail.com", "first2", "last2", "hash2");
        int id3 = people.AddPerson(transaction, "p3@hotmail.com", "first3", "last3", "hash3");

        KeyValueTableArray keyValueTableArray = people.GetPeople(transaction);
        ASSERT_EQ(keyValueTableArray.size(), 3u);

        KeyValueTableArray keyValueTableArrayExpected = 
        {
            {   {std::string(DbSchema::kPeopleId), std::to_string(id1)},
                {std::string(DbSchema::kPeopleEmail), "p1@hotmail.com"},
                {std::string(DbSchema::kPeopleFirstName), "first1"},
                {std::string(DbSchema::kPeopleLastName), "last1"},
                {std::string(DbSchema::kPeoplePasswordHash), "hash1"} 
            },
            {   {std::string(DbSchema::kPeopleId), std::to_string(id2)},
                {std::string(DbSchema::kPeopleEmail), "p2@hotmail.com"},
                {std::string(DbSchema::kPeopleFirstName), "first2"},
                {std::string(DbSchema::kPeopleLastName), "last2"},
                {std::string(DbSchema::kPeoplePasswordHash), "hash2"} 
            },
            {   {std::string(DbSchema::kPeopleId), std::to_string(id3)},
                {std::string(DbSchema::kPeopleEmail), "p3@hotmail.com"},
                {std::string(DbSchema::kPeopleFirstName), "first3"},
                {std::string(DbSchema::kPeopleLastName), "last3"},
                {std::string(DbSchema::kPeoplePasswordHash), "hash3"} 
            }
        };
        ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
            keyValueTableArray,
            keyValueTableArrayExpected,
            std::string(DbSchema::kPeopleCreatedAt),
            std::string(DbSchema::kPeopleUpdatedAt),
            std::string(DbSchema::kPeopleEmailVerifiedAt),
            std::string(DbSchema::kPeopleMustChangePassword),
            std::string(DbSchema::kPeopleFailedLoginAttempts),
            std::string(DbSchema::kPeopleLockedUntil)));
        });
}

TEST(PeopleTableTest, GetPeopleEmpty) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetPeopleEmpty", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        KeyValueTableArray keyValueTableArray = people.GetPeople(transaction);
        EXPECT_TRUE(keyValueTableArray.empty());
        });
}

// GetPersonById
TEST(PeopleTableTest, GetPersonByIdBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetPersonByIdBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");
        people.AddPerson(
            transaction, "p2@hotmail.com", "first2", "last2", "hash2");

        KeyValueTable keyValueTable = people.GetPersonById(transaction, id1);
        KeyValueTable expected = testDatabaseUtil.PersonKeyValueTable(
            id1, "p1@hotmail.com", "first1", "last1", "hash1");
        ASSERT_TRUE(CompareKeyValueTablesMinusColumns(
            keyValueTable,
            expected,
            std::string(DbSchema::kPeopleCreatedAt),
            std::string(DbSchema::kPeopleUpdatedAt),
            std::string(DbSchema::kPeopleEmailVerifiedAt),
            std::string(DbSchema::kPeopleMustChangePassword),
            std::string(DbSchema::kPeopleFailedLoginAttempts),
            std::string(DbSchema::kPeopleLockedUntil)));
        });
}

TEST(PeopleTableTest, GetPersonByIdNotFound) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetPersonByIdNotFound", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        KeyValueTable keyValueTable = people.GetPersonById(transaction, 999999);
        EXPECT_TRUE(keyValueTable.empty());
        });
}

// LookupPersonByEmail
TEST(PeopleTableTest, LookupPersonByEmailBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("LookupPersonByEmailBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        people.AddPerson(transaction, "p1@hotmail.com", "first1", "last1", "hash1");
        int id2 = people.AddPerson(transaction, "p2@hotmail.com", "first2", "last2", "hash2");

        KeyValueTable keyValueTable = people.LookupPersonByEmail(transaction, "p2@hotmail.com");
        KeyValueTable expected = testDatabaseUtil.PersonKeyValueTable(
            id2, "p2@hotmail.com", "first2", "last2", "hash2");
        ASSERT_TRUE(CompareKeyValueTablesMinusColumns(
            keyValueTable,
            expected,
            std::string(DbSchema::kPeopleCreatedAt),
            std::string(DbSchema::kPeopleUpdatedAt),
            std::string(DbSchema::kPeopleEmailVerifiedAt),
            std::string(DbSchema::kPeopleMustChangePassword),
            std::string(DbSchema::kPeopleFailedLoginAttempts),
            std::string(DbSchema::kPeopleLockedUntil)));
        });
}

TEST(PeopleTableTest, LookupPersonByEmailNotFound) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("LookupPersonByEmailNotFound", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        KeyValueTable keyValueTable = people.LookupPersonByEmail(transaction, "nope@example.com");
        EXPECT_TRUE(keyValueTable.empty());
        });
}

TEST(PeopleTableTest, GetCreatedAtBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetCreatedAtBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "person1@hotmail.com", "first1", "last1", "hash1");
        ASSERT_GT(id1, 0);

        std::string createdAt = people.GetCreatedAt(transaction, id1);
        ASSERT_FALSE(createdAt.empty());

        // Format example: "January 8, 2026"
        // Month: full month name
        // Day: 1-31 (1 or 2 digits)
        // Year: 20xx (4 digits starting with 20)
        const std::regex kCreatedAtRegex(
            R"(^(January|February|March|April|May|June|July|August|September|October|November|December) ([1-9]|[12][0-9]|3[01]), 20\d{2}$)");

        EXPECT_TRUE(std::regex_match(createdAt, kCreatedAtRegex))
            << "Unexpected created_at display format: " << createdAt;
        });
}

constexpr int64_t kWeekInMicros = 7LL * 24LL * 60LL * 60LL * 1000000LL;
constexpr int64_t kTwoYearsInMicros = 2LL * 365LL * 24LL * 60LL * 60LL * 1000000LL;

// VerifyPersonEmail
TEST(PeopleTableTest, VerifyPersonEmailBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("VerifyPersonEmailBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        // First lookup to capture initial timestamps and null verified_at
        KeyValueTable keyValueTable1 = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!keyValueTable1.empty());

        std::string createdAt1 = keyValueTable1[std::string(DbSchema::kPeopleCreatedAt)];
        int64_t createdUs = std::stoll(createdAt1);
        int64_t updated1Us = std::stoll(keyValueTable1[std::string(DbSchema::kPeopleUpdatedAt)]);
        EXPECT_TRUE(keyValueTable1[std::string(DbSchema::kPeopleEmailVerifiedAt)].empty());

        // Pass the initial created_at value back to VerifyPersonEmail
        people.VerifyPersonEmail(transaction, "p1@hotmail.com");

        // Second lookup to verify timestamps changed as expected
        KeyValueTable keyValueTable2 = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!keyValueTable2.empty());

        std::string createdAt2 = keyValueTable2[std::string(DbSchema::kPeopleCreatedAt)];
        int64_t updated2Us = std::stoll(keyValueTable2[std::string(DbSchema::kPeopleUpdatedAt)]);

        // created_at should remain the same
        EXPECT_EQ(createdAt2, createdAt1);

        // updated_at should be later
        ASSERT_TRUE(updated2Us > updated1Us);

        // verified_at should now be set and later than created_at
        ASSERT_FALSE(keyValueTable2[std::string(DbSchema::kPeopleEmailVerifiedAt)].empty());
        int64_t verifiedUs = std::stoll(keyValueTable2[std::string(DbSchema::kPeopleEmailVerifiedAt)]);
        ASSERT_TRUE(verifiedUs >= createdUs);
        });
}

TEST(PeopleTableTest, VerifyPersonEmailNonexistentEmail) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("VerifyPersonEmailNonexistentEmail", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        try {
            // Using an arbitrary created_at; for non-existent id the call should not throw.
            people.VerifyPersonEmail(transaction, "nobody@example.com");
            FAIL() << "Expected exception for nonexistent id";
        } catch (const std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "People::VerifyPersonEmail - Person not found");
        }
        KeyValueTable keyValueTable = people.GetPersonById(transaction, 424242);
        EXPECT_TRUE(keyValueTable.empty());
        });
}

TEST(PeopleTableTest, VerifyPersonEmailEmailVerifiedAlreadySet) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("VerifyPersonEmailEmailVerifiedAlreadySet", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        // First, verify successfully to set email_verified_at
        KeyValueTable keyValueTable = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!keyValueTable.empty());
        people.VerifyPersonEmail(transaction, "p1@hotmail.com");

        // Now a second attempt should throw "already set"
        try {
            people.VerifyPersonEmail(transaction, "p1@hotmail.com");
            FAIL() << "Expected runtime_error for already verified email";
        } catch (const std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "People::VerifyPersonEmail - EmailVerifiedAt already set");
        } catch (const std::exception& e) {
            FAIL() << "Expected std::runtime_error, got: " << e.what();
        }
        });
}

// UpdatePerson
TEST(PeopleTableTest, UpdatePersonBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("UpdatePersonBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        // Snapshot updated_at prior to update
        KeyValueTable before = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!before.empty());
        std::string updatedBefore = before[std::string(DbSchema::kPeopleUpdatedAt)];
        ASSERT_FALSE(updatedBefore.empty());
        int64_t updatedBeforeUs = stoll(updatedBefore);

        KeyValueTable changes = {
            { std::string(DbSchema::kPeopleFirstName), "first1b" },
            { std::string(DbSchema::kPeopleLastName), "last1b" }
        };
        people.UpdatePerson(transaction, id1, changes);

        KeyValueTable keyValueTable = people.GetPersonById(transaction, id1);
        EXPECT_EQ(keyValueTable[std::string(DbSchema::kPeopleFirstName)], "first1b");
        EXPECT_EQ(keyValueTable[std::string(DbSchema::kPeopleLastName)], "last1b");
        EXPECT_EQ(keyValueTable[std::string(DbSchema::kPeopleEmail)], "p1@hotmail.com"); // unchanged

        // Verify updated_at changed and is later
        std::string updatedAfter = keyValueTable[std::string(DbSchema::kPeopleUpdatedAt)];
        ASSERT_FALSE(updatedAfter.empty());
        int64_t updatedAfterUs = stoll(updatedAfter);
        ASSERT_TRUE(updatedAfterUs > updatedBeforeUs);
        });
}

TEST(PeopleTableTest, UpdatePersonInvalidColumnName) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("UpdatePersonInvalidColumnName", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        KeyValueTable changes = {
            { "does_not_exist", "x" }
        };
        try {
            people.UpdatePerson(transaction, id1, changes);
            FAIL() << "Expected invalid_argument for invalid column name";
        } catch (const std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "People::UpdatePerson - key not allowed");
        } catch (const std::exception& e) {
            FAIL() << "Expected std::invalid_argument, got: " << e.what();
        }
        });
}

// UpdatePassword
TEST(PeopleTableTest, UpdatePasswordBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("UpdatePasswordBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        // Snapshot updated_at prior to password change
        KeyValueTable before = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!before.empty());
        int64_t updatedBeforeUs = stoll(before[std::string(DbSchema::kPeopleUpdatedAt)]);

        people.UpdatePassword(transaction, id1, "newhash");
        KeyValueTable keyValueTable = people.GetPersonById(transaction, id1);
        EXPECT_EQ(keyValueTable[std::string(DbSchema::kPeoplePasswordHash)], "newhash");
        // Verify updated_at changed and is later
        int64_t updatedAfterUs = stoll(keyValueTable[std::string(DbSchema::kPeopleUpdatedAt)]);
        ASSERT_TRUE(updatedAfterUs > updatedBeforeUs);
        });
}

TEST(PeopleTableTest, UpdatePasswordNonexistentId) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("UpdatePasswordNonexistentId", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        // Should not throw on non-existent id; it just updates 0 rows.
        try {
            people.UpdatePassword(transaction, 555555, "newhash");
        } catch (const std::exception& e) {
            FAIL() << "Did not expect exception: " << e.what();
        }
        KeyValueTable keyValueTable = people.GetPersonById(transaction, 555555);
        EXPECT_TRUE(keyValueTable.empty());
        });
}

// SetMustChangePassword
TEST(PeopleTableTest, SetMustChangePasswordRoundTrips) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("SetMustChangePassword", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        KeyValueTable before = people.GetPersonById(transaction, id1);
        ASSERT_TRUE(!before.empty());
        // Default is false.
        EXPECT_EQ(before[std::string(DbSchema::kPeopleMustChangePassword)], "f");
        int64_t updatedBeforeUs = stoll(before[std::string(DbSchema::kPeopleUpdatedAt)]);

        people.SetMustChangePassword(transaction, id1, true);
        KeyValueTable after = people.GetPersonById(transaction, id1);
        EXPECT_EQ(after[std::string(DbSchema::kPeopleMustChangePassword)], "t");
        // Other fields untouched; updated_at bumped.
        EXPECT_EQ(after[std::string(DbSchema::kPeopleEmail)], "p1@hotmail.com");
        EXPECT_EQ(after[std::string(DbSchema::kPeoplePasswordHash)], "hash1");
        int64_t updatedAfterUs = stoll(after[std::string(DbSchema::kPeopleUpdatedAt)]);
        ASSERT_TRUE(updatedAfterUs > updatedBeforeUs);

        people.SetMustChangePassword(transaction, id1, false);
        KeyValueTable cleared = people.GetPersonById(transaction, id1);
        EXPECT_EQ(cleared[std::string(DbSchema::kPeopleMustChangePassword)], "f");
        });
}

// must_change_password is an auth flag — the generic profile update must
// keep rejecting it (the dedicated setter above is the only write path).
TEST(PeopleTableTest, UpdatePersonStillRejectsMustChangePassword) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("UpdatePersonRejectsMcp", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        KeyValueTable changes = {
            { std::string(DbSchema::kPeopleMustChangePassword), "true" }
        };
        try {
            people.UpdatePerson(transaction, id1, changes);
            FAIL() << "Expected runtime_error for disallowed column";
        } catch (const std::runtime_error& e) {
            ASSERT_EQ(std::string(e.what()), "People::UpdatePerson - key not allowed");
        }
        });
}

// DeletePerson
TEST(PeopleTableTest, DeletePersonBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeletePersonBasic", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        int id1 = people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");
        int id2 = people.AddPerson(
            transaction, "p2@hotmail.com", "first2", "last2", "hash2");

        people.DeletePerson(transaction, id1);

        KeyValueTable keyValueTable1 = people.GetPersonById(transaction, id1);
        EXPECT_TRUE(keyValueTable1.empty());
        KeyValueTable keyValueTable2 = people.GetPersonById(transaction, id2);
        ASSERT_TRUE(!keyValueTable2.empty());
        EXPECT_EQ(keyValueTable2[std::string(DbSchema::kPeopleEmail)], "p2@hotmail.com");
        });
}

TEST(PeopleTableTest, DeletePersonNonexistentId) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("DeletePersonNonexistentId", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        People people(databaseHelper);
        people.AddPerson(
            transaction, "p1@hotmail.com", "first1", "last1", "hash1");

        // Should not throw and should keep existing records intact
        try {
            people.DeletePerson(transaction, 999999);
        } catch (const std::exception& e) {
            FAIL() << "Did not expect exception: " << e.what();
        }

        KeyValueTableArray keyValueTableArray = people.GetPeople(transaction);
        ASSERT_EQ(keyValueTableArray.size(), 1u);
        EXPECT_EQ(keyValueTableArray[0][std::string(DbSchema::kPeopleEmail)], "p1@hotmail.com");
        });
}

// TODO: If schema uses triggers/defaults for created_at/updated_at, add tests to validate them.
// TODO: Add email format validation tests if constraints are present at the DB layer.

} // namespace
} // namespace TableHelpers