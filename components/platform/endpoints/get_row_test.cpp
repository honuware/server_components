#include "get_row.h"
#include "web_app.h"
#include "test/src/util/test_helper.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "db_schema/config_secrets.h"
#include "db_schema/people.h"
#include "db_schema/roles.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/table_helpers/admin_column_redactions.h"
#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/roles.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "util/error_codes.h"
#include "util/json_value.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "endpoints/endpoint_test_helper.h"

namespace {

using ::testing::Eq;

void MakeTestPeopleTable(TestDatabaseUtil& testDb, Transaction& transaction) {
    auto databaseInfo = testDb.GetDatabaseInfo();
    constexpr std::string_view kTestPeople = "test_people";
    databaseInfo.AddTable(kTestPeople);
    databaseInfo.AddColumnPrimaryKey(kTestPeople, "person_id", DB_TYPE_SERIAL);
    databaseInfo.AddColumnUnique(kTestPeople, "email", DB_TYPE_STRING);
    databaseInfo.AddColumnSimple(kTestPeople, "first_name", DB_TYPE_STRING);
    databaseInfo.AddColumnSimple(kTestPeople, "last_name", DB_TYPE_STRING);
    databaseInfo.AddColumnSimple(kTestPeople, "password_hash", DB_TYPE_STRING);
    DbOps::CreateTable(transaction, databaseInfo, kTestPeople);
}

// Expected JSON for id1
constexpr std::string_view kRowFrag1 = R"JSON(
    {
        "sortedColumnNames": [
            "email",
            "first_name",
            "last_name",
            "password_hash",
            "person_id"
        ],
        "dataTable": [
            ["person1@hotmail.com", "first1", "last1", "hash1", "{id1}"]
        ]
    }
    )JSON";

// Expected JSON for id2
constexpr std::string_view kRowFrag2 = R"JSON(
    {
        "sortedColumnNames": [
            "email",
            "first_name",
            "last_name",
            "password_hash",
            "person_id"
        ],
        "dataTable": [
            ["person2@hotmail.com", "first2", "last2", "hash2", "{id2}"]
        ]
    }
    )JSON";

int64_t AddPerson(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view hash) {
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction, databaseHelper, "test_people",
        { DbPair("email", email),
         DbPair("first_name", firstName),
         DbPair("last_name", lastName),
         DbPair("password_hash", hash) });
}

TEST(GetRowEndpointTest, GetRowBasic) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetRowBasic", [&](Transaction& transaction) {
        auto databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        MakeTestPeopleTable(testDatabaseUtil, transaction);
        EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

        endpointTestHelper.AddAllowedTable(transaction, "test_people");

        int64_t id1 = AddPerson(transaction, databaseHelper,
            "person1@hotmail.com", "first1", "last1", "hash1");
        int64_t id2 = AddPerson(transaction, databaseHelper,
            "person2@hotmail.com", "first2", "last2", "hash2");

        KeyValueTable formatReplace1 = { {"id1", StringFromInt(id1)} };
        std::string jsonComp1 = FormatString(kRowFrag1, formatReplace1);
        KeyValueTable formatReplace2 = { {"id2", StringFromInt(id2)} };
        std::string jsonComp2 = FormatString(kRowFrag2, formatReplace2);

        crow::request req;
        crow::response resp;

        req.url = "/api/get_row/test_people/person_id/" + StringFromInt(id1);
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);
        ASSERT_EQ(resp.code, 200);
        EXPECT_THAT(
			Json::Value::FromText(resp.body),
			JsonValueIs(Json::Value::FromText(jsonComp1)));

        resp.clear();

        req.url = "/api/get_row/test_people/person_id/" + StringFromInt(id2);
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);
        ASSERT_EQ(resp.code, 200);
        EXPECT_THAT(
			Json::Value::FromText(resp.body),
			JsonValueIs(Json::Value::FromText(jsonComp2)));
        });
}

TEST(GetRowEndpointTest, GetRowNotAllowed) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetRowNotAllowed", [&](Transaction& transaction) {
        auto databaseHelper = testDatabaseUtil.GetDatabaseHelper();
        MakeTestPeopleTable(testDatabaseUtil, transaction);
        EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

        int64_t id1 = AddPerson(transaction, databaseHelper,
            "person1@hotmail.com", "first1", "last1", "hash1");

        crow::request req;
        crow::response resp;

        req.url = "/api/get_row/test_people/person_id/" + StringFromInt(id1);
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);
        ASSERT_EQ(resp.code, 400);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
        EXPECT_EQ(body["detail"].Get<std::string>(), "Table(test_people) is not an allowed table.");
        });
}

// Phase 1.8 of the security review: the config_secrets table holds live
// credentials (Square access token, mail app password, …). It must never be
// reachable through the generic CRUD endpoints — neither anonymously nor as
// an admin. A dedicated /api/admin/secrets endpoint (Phase 8.3) handles
// reads and writes with masking and audit logging.
TEST(GetRowEndpointTest, GetRowConfigSecretsAnonymousForbidden) {
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetRowConfigSecretsAnonymousForbidden",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

        crow::request req;
        crow::response resp;
        req.url = "/api/get_row/config_secrets/id/1";
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

        ASSERT_EQ(resp.code, 400);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
        EXPECT_EQ(body["detail"].Get<std::string>(),
            "Table(config_secrets) is not an allowed table.");
        });
}

TEST(GetRowEndpointTest, GetRowConfigSecretsAdminForbidden) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetRowConfigSecretsAdminForbidden",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

        // Populate admin_top_level_tables with a sentinel table so the admin
        // does have *some* generic CRUD access — that way this test would
        // catch a regression where config_secrets gets accidentally
        // re-added to admin_top_level_tables.
        endpointTestHelper.AddAdminTopLevelTable(
            transaction, DbSchema::kRolesTable);

        auto secrets = endpointTestHelper.GetSecretsHelper();
        secrets->AddSecret(transaction,
            Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        // Create an admin user with an active session and the admin role.
        Auth::PersonHelper personHelper(testDatabaseUtil.GetDatabaseHelper());
        Auth::PersonInfo info{
            "admin-cs@example.com", "Admin", "ConfigSecretTester" };
        personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));

        TableHelpers::Roles roles(testDatabaseUtil.GetDatabaseHelper());
        int64_t adminRoleId = roles.AddRole(
            transaction,
            std::string(DbSchema::kRoleNameAdmin),
            "Administrator");

        TableHelpers::People people(testDatabaseUtil.GetDatabaseHelper());
        int64_t personId = std::stoll(
            people.LookupPersonByEmail(transaction, info.email)
                .at(std::string(DbSchema::kPeopleId)));

        TableHelpers::RoleAssignments ra(
            testDatabaseUtil.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, adminRoleId);

        auto cookieMgr = endpointTestHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        // Sanity-check: the admin can hit the sentinel admin table.
        crow::request adminReq;
        crow::response adminResp;
        adminReq.url = "/api/get_row/roles/id/" + std::to_string(adminRoleId);
        endpointTestHelper.GetWebApp().GetApp().handle_full(adminReq, adminResp);
        ASSERT_EQ(adminResp.code, 200)
            << "admin login isn't working — fix this before relying on the "
            << "config_secrets assertion below";

        // The actual regression: even an admin cannot reach config_secrets
        // through the generic CRUD path.
        crow::request req;
        crow::response resp;
        req.url = "/api/get_row/config_secrets/id/1";
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

        ASSERT_EQ(resp.code, 400);
        Json::Value body = Json::Value::FromText(resp.body);
        EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
        EXPECT_EQ(body["detail"].Get<std::string>(),
            "Table(config_secrets) is not an allowed table.");
        });

    Auth::ServerConfig::Shutdown();
}

// Phase 3.8 of the security review: end-to-end check that
// /api/get_row/people/... never returns the password_hash column,
// even to a logged-in admin who otherwise has access to the table.
//
// Setup mirrors GetRowConfigSecretsAdminForbidden — admin session,
// admin role, people in admin_top_level_tables — but here the
// expectation is success (200) with a redacted column list.
TEST(GetRowEndpointTest, GetRowPeopleHidesPasswordHash) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("GetRowPeopleHidesPasswordHash",
        [&](Transaction& transaction) {

        // Insert the (people, password_hash) redaction BEFORE
        // constructing EndpointTestHelper, since the helper
        // snapshots the table contents into the WebApp at
        // construction time.
        TableHelpers::AdminColumnRedactions redactions(
            testDatabaseUtil.GetDatabaseHelper());
        redactions.AddAdminColumnRedaction(
            transaction, DbSchema::kPeopleTable, DbSchema::kPeoplePasswordHash);

        EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

        endpointTestHelper.AddAdminTopLevelTable(
            transaction, DbSchema::kPeopleTable);

        auto secrets = endpointTestHelper.GetSecretsHelper();
        secrets->AddSecret(transaction,
            Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        Auth::PersonHelper personHelper(testDatabaseUtil.GetDatabaseHelper());
        Auth::PersonInfo info{
            "admin-redact@example.com", "Admin", "Redactor" };
        personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, info.email, sessionToken));

        TableHelpers::Roles roles(testDatabaseUtil.GetDatabaseHelper());
        int64_t adminRoleId = roles.AddRole(
            transaction,
            std::string(DbSchema::kRoleNameAdmin),
            "Administrator");

        TableHelpers::People people(testDatabaseUtil.GetDatabaseHelper());
        int64_t personId = std::stoll(
            people.LookupPersonByEmail(transaction, info.email)
                .at(std::string(DbSchema::kPeopleId)));

        TableHelpers::RoleAssignments ra(
            testDatabaseUtil.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, adminRoleId);

        auto cookieMgr = endpointTestHelper.GetCookieManagerTest();
        Auth::CookieProperties cp;
        cp.path = "/";
        cp.sameSite = Auth::CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        crow::request req;
        crow::response resp;
        req.url = "/api/get_row/people/id/" + std::to_string(personId);
        endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

        ASSERT_EQ(resp.code, 200);
        Json::Value body = Json::Value::FromText(resp.body);
        ASSERT_TRUE(body.HasChild("sortedColumnNames", nullptr));
        for (const auto& col : body["sortedColumnNames"].GetArray()) {
            EXPECT_NE(col.Get<std::string>(), "password_hash")
                << "password_hash leaked through /api/get_row/people";
        }
        // Sanity-check: the row was actually returned (we got the right
        // person back), so the redaction isn't silently emptying the
        // result. The admin's email comes through unredacted.
        bool sawEmail = false;
        for (const auto& row : body["dataTable"].GetArray()) {
            for (const auto& cell : row.GetArray()) {
                std::string s = cell.Is<std::string>()
                    ? cell.Get<std::string>()
                    : cell.ToSimpleString();
                if (s == "admin-redact@example.com") sawEmail = true;
                // Belt-and-suspenders: no field should hold a hash-shaped
                // value. CreateFullyValidatedUser produces an Argon2 hash
                // beginning with "$argon2".
                EXPECT_EQ(s.find("$argon2"), std::string::npos)
                    << "Argon2 hash leaked through generic CRUD response";
            }
        }
        EXPECT_TRUE(sawEmail) << "expected the admin's email in the response";
        });

    Auth::ServerConfig::Shutdown();
}

} // namespace