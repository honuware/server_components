#include "endpoints/get_table_rows.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "business_logic/auth/person.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "db_schema/device_tokens.h"
#include "db_schema/email_verifications.h"
#include "db_schema/auth_events.h"
#include "db_schema/people.h"
#include "db_schema/permission_implications.h"
#include "db_schema/admin_alerts.h"
#include "db_schema/roles.h"
#include "db_schema/idempotency_keys.h"
#include "db_schema/login_attempts.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "sql_util/table_helpers/admin_column_redactions.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/table_helpers/people.h"
#include "db_schema/role_permissions.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/database_common.h"
#include "sql_util/json/database_rest_helper.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/test_helper.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using JsonTestUtil::JsonPerson;

		int64_t IdFromPrimaryKey(const Json::Value& value) {
			return std::stoll(value["id"].Get<std::string>());
        }

        // Find a row in a dataTable by a column value.
        // Returns the row array, or nullptr if not found.
        const Json::Value* FindRowByColumn(
            const Json::Value& body,
            const std::string& columnName,
            const std::string& expectedValue) {
            const auto& cols = body["sortedColumnNames"].GetArray();
            int colIdx = -1;
            for (size_t i = 0; i < cols.size(); ++i) {
                if (cols[i].Get<std::string>() == columnName) {
                    colIdx = static_cast<int>(i);
                    break;
                }
            }
            if (colIdx < 0) return nullptr;
            for (const auto& row : body["dataTable"].GetArray()) {
                if (row[static_cast<size_t>(colIdx)].Get<std::string>() == expectedValue) {
                    return &row;
                }
            }
            return nullptr;
        }

        TEST(GetTableRowsTest, GetTableRowsBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetTableRowsBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person1 = JsonPerson(
                    "person1@hotmail.com", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "person2@hotmail.com", "first2", "last2", "hash2");

                DatabaseRESTHelper databaseRestHelper(databaseHelper);
                int64_t id1 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person1));
                int64_t id2 = IdFromPrimaryKey(
                    databaseRestHelper.AddTableValueFetchPrimaryKey(
                        transaction, databaseInfo, DbSchema::kPeopleTable,
                        person2));

                crow::request req;
                req.url = "/api/get_table_rows/people";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                Json::Value body = Json::Value::FromText(resp.body);
                ASSERT_EQ(body["dataTable"].GetArray().size(), 2u);

                // Find each person by email (order-independent)
                const auto* row1 = FindRowByColumn(body, "email", "person1@hotmail.com");
                const auto* row2 = FindRowByColumn(body, "email", "person2@hotmail.com");
                ASSERT_NE(row1, nullptr);
                ASSERT_NE(row2, nullptr);

                // Verify person1 fields
                const auto& cols = body["sortedColumnNames"].GetArray();
                // Column order: email(0), first_name(1), id(2), last_name(3), password_hash(4)
                // But find indices dynamically to be safe
                int emailIdx = -1, fnIdx = -1, idIdx = -1, lnIdx = -1, phIdx = -1;
                for (size_t i = 0; i < cols.size(); ++i) {
                    std::string col = cols[i].Get<std::string>();
                    if (col == "email") emailIdx = static_cast<int>(i);
                    else if (col == "first_name") fnIdx = static_cast<int>(i);
                    else if (col == "id") idIdx = static_cast<int>(i);
                    else if (col == "last_name") lnIdx = static_cast<int>(i);
                    else if (col == "password_hash") phIdx = static_cast<int>(i);
                }
                ASSERT_GE(emailIdx, 0);
                ASSERT_GE(fnIdx, 0);
                ASSERT_GE(idIdx, 0);
                ASSERT_GE(lnIdx, 0);
                ASSERT_GE(phIdx, 0);

                EXPECT_EQ((*row1)[fnIdx].Get<std::string>(), "first1");
                EXPECT_EQ((*row1)[lnIdx].Get<std::string>(), "last1");
                EXPECT_EQ((*row1)[phIdx].Get<std::string>(), "hash1");
                EXPECT_EQ((*row1)[idIdx].Get<std::string>(), std::to_string(id1));

                EXPECT_EQ((*row2)[fnIdx].Get<std::string>(), "first2");
                EXPECT_EQ((*row2)[lnIdx].Get<std::string>(), "last2");
                EXPECT_EQ((*row2)[phIdx].Get<std::string>(), "hash2");
                EXPECT_EQ((*row2)[idIdx].Get<std::string>(), std::to_string(id2));
                });
        }

        TEST(GetTableRowsTest, GetTableRowsTableNotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("GetTableRowsTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

				Json::Value person1 = JsonPerson(
                    "person1@hotmail.com", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "person2@hotmail.com", "first2", "last2", "hash2");

				Json::JsonArray valueArray;
				valueArray.emplace_back(std::move(person1));
				valueArray.emplace_back(std::move(person2));
				Json::Value peopleList(std::move(valueArray));

                crow::request req;
                req.url = "/api/get_table_rows/people";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                });
        }

        TEST(GetTableRowsTest, AdminCanAccessAdminOnlyTable) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("AdminCanAccessAdminOnlyTable", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Set up allowed_tables (for all users) and admin_top_level_tables (for admins)
                endpointHelper.AddAllowedTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAllowedTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kRolesTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create admin user
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "admin@example.com", "Admin", "User" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));

                // Assign admin role
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                int64_t adminRoleId = roles.AddRole(transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");
                TableHelpers::People people(testDb.GetDatabaseHelper());
                KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
                int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));
                TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
                ra.AddRoleAssignment(transaction, personId, adminRoleId);

                // Set session cookie
                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // Admin should be able to access admin-only table (roles)
                crow::request req;
                req.url = "/api/get_table_rows/roles";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 200);
            });

            Auth::ServerConfig::Shutdown();
        }

        TEST(GetTableRowsTest, NonAdminCannotAccessAdminOnlyTable) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NonAdminCannotAccessAdminOnlyTable", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Set up allowed_tables (for all users) and admin_top_level_tables (for admins)
                endpointHelper.AddAllowedTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAllowedTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kRolesTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create non-admin user (no admin role)
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "user@example.com", "Regular", "User" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));

                // Create admin role but do NOT assign it to this user
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                roles.AddRole(transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

                // Set session cookie
                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // Non-admin should NOT be able to access admin-only table (roles)
                crow::request req;
                req.url = "/api/get_table_rows/roles";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
            });

            Auth::ServerConfig::Shutdown();
        }

        TEST(GetTableRowsTest, NonAdminCanAccessAllowedTable) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NonAdminCanAccessAllowedTable", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Set up allowed_tables (for all users)
                endpointHelper.AddAllowedTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAllowedTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kRolesTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create non-admin user
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "user@example.com", "Regular", "User" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, info.email, sessionToken));

                // Create admin role but do NOT assign
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                roles.AddRole(transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

                // Set session cookie
                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // Non-admin should still access allowed tables (admin_alerts)
                crow::request req;
                req.url = "/api/get_table_rows/admin_alerts";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                EXPECT_EQ(resp.code, 200);
            });

            Auth::ServerConfig::Shutdown();
        }

        TEST(GetTableRowsTest, RecordPermissionGrantsTableAccess) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("RecordPermissionGrantsTableAccess", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Set up allowed_tables and admin tables
                endpointHelper.AddAllowedTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kAdminAlertsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kRolesTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create the manage_records permission
                TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
                int64_t manageRecordsPermId = perms.AddPermission(
                    transaction, "manage_records", "Manage records permission");

                // Map the admin_alerts table to manage_records permission
                endpointHelper.AddAdminTablePermission(
                    transaction, "admin_alerts", manageRecordsPermId);

                // Create a role with manage_records permission
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                int64_t recordManagerRoleId = roles.AddRole(
                    transaction, "record_manager", "Records manager role");
                // Also create admin role so IsAdmin check doesn't throw
                roles.AddRole(
                    transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

                TableHelpers::RolePermissions rp(testDb.GetDatabaseHelper());
                rp.AddRolePermission(transaction, recordManagerRoleId, manageRecordsPermId);

                // Create user and assign record_manager role (not admin)
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "manager@example.com", "Record", "Manager" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(
                    transaction, secrets, info.email, sessionToken));

                TableHelpers::People people(testDb.GetDatabaseHelper());
                KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
                int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

                TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
                ra.AddRoleAssignment(transaction, personId, recordManagerRoleId);

                // Set session cookie
                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // User with manage_records should access the admin_alerts table
                {
                    crow::request req;
                    req.url = "/api/get_table_rows/admin_alerts";
                    crow::response resp;
                    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
                    EXPECT_EQ(resp.code, 200);
                }

                // User with manage_records should NOT access roles (admin-only)
                {
                    crow::request req;
                    req.url = "/api/get_table_rows/roles";
                    crow::response resp;
                    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
                    EXPECT_EQ(resp.code, 400);
                }
            });

            Auth::ServerConfig::Shutdown();
        }

        TEST(GetTableRowsTest, NoPermissionsCannotAccessAdminTables) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("NoPermissionsCannotAccessAdminTables", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                endpointHelper.AddAllowedTable(transaction, DbSchema::kDeviceTokensTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kAdminAlertsTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create manage_records permission and map admin_alerts to it
                TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
                int64_t manageRecordsPermId = perms.AddPermission(
                    transaction, "manage_records", "Manage records permission");
                endpointHelper.AddAdminTablePermission(
                    transaction, "admin_alerts", manageRecordsPermId);

                // Create admin role so IsAdmin check works
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                roles.AddRole(
                    transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

                // Create user with no roles/permissions
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "noroles@example.com", "No", "Roles" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(
                    transaction, secrets, info.email, sessionToken));

                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // User with no permissions should NOT access admin_alerts
                crow::request req;
                req.url = "/api/get_table_rows/admin_alerts";
                crow::response resp;
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
                EXPECT_EQ(resp.code, 400);

                // But should still access public allowed tables
                crow::request req2;
                req2.url = "/api/get_table_rows/device_tokens";
                crow::response resp2;
                endpointHelper.GetWebApp().GetApp().handle_full(req2, resp2);
                EXPECT_EQ(resp2.code, 200);
            });

            Auth::ServerConfig::Shutdown();
        }

        TEST(GetTableRowsTest, RecordPermissionCanAccessNewlyConfiguredAdminTables) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDb;
            testDb.RunInTransaction("RecordPermissionCanAccessNewlyConfiguredAdminTables", [&](Transaction& transaction) {
                EndpointTestHelper endpointHelper(transaction, testDb);

                // Register tables as admin top-level and nested
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kEmailVerificationsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kLoginAttemptsTable);
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kAuthEventsTable);
                endpointHelper.AddAdminNestedTable(transaction, DbSchema::kPermissionImplicationsTable);
                endpointHelper.AddAdminNestedTable(transaction, DbSchema::kIdempotencyKeysTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                // Create manage_records permission and map tables to it
                TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
                int64_t manageRecordsPermId = perms.AddPermission(
                    transaction, "manage_records", "Manage records permission");

                endpointHelper.AddAdminTablePermission(transaction, "email_verifications", manageRecordsPermId);
                endpointHelper.AddAdminTablePermission(transaction, "login_attempts", manageRecordsPermId);
                endpointHelper.AddAdminTablePermission(transaction, "auth_events", manageRecordsPermId);
                endpointHelper.AddAdminTablePermission(transaction, "permission_implications", manageRecordsPermId);
                endpointHelper.AddAdminTablePermission(transaction, "idempotency_keys", manageRecordsPermId);

                // Create a role with manage_records permission
                TableHelpers::Roles roles(testDb.GetDatabaseHelper());
                int64_t recordManagerRoleId = roles.AddRole(
                    transaction, "record_manager", "Records manager role");
                roles.AddRole(
                    transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

                TableHelpers::RolePermissions rp(testDb.GetDatabaseHelper());
                rp.AddRolePermission(transaction, recordManagerRoleId, manageRecordsPermId);

                // Create user and assign record_manager role
                Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
                Auth::PersonInfo info{ "manager@example.com", "Record", "Manager" };
                personHelper.CreateFullyValidatedUser(transaction, info, "Password!");

                std::string sessionToken;
                ASSERT_TRUE(personHelper.CreateSessionToken(
                    transaction, secrets, info.email, sessionToken));

                TableHelpers::People people(testDb.GetDatabaseHelper());
                KeyValueTable person = people.LookupPersonByEmail(transaction, info.email);
                int64_t personId = std::stoll(person.at(std::string(DbSchema::kPeopleId)));

                TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
                ra.AddRoleAssignment(transaction, personId, recordManagerRoleId);

                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                // Verify access to newly configured top-level tables
                auto testAccess = [&](const std::string& tableName, int expectedCode) {
                    crow::request req;
                    req.url = "/api/get_table_rows/" + tableName;
                    crow::response resp;
                    endpointHelper.GetWebApp().GetApp().handle_full(req, resp);
                    EXPECT_EQ(resp.code, expectedCode)
                        << "Table: " << tableName << " expected " << expectedCode
                        << " but got " << resp.code;
                };

                testAccess("email_verifications", 200);
                testAccess("login_attempts", 200);
                testAccess("auth_events", 200);

                // Verify access to newly configured nested tables
                testAccess("permission_implications", 200);
                testAccess("idempotency_keys", 200);

                // Verify still cannot access unmapped admin table
                endpointHelper.AddAdminTopLevelTable(transaction, DbSchema::kRolesTable);
                testAccess("roles", 400);
            });

            Auth::ServerConfig::Shutdown();
        }

        // Phase 3.8 of the security review: end-to-end check that
        // /api/get_table_rows/people never returns the password_hash
        // column, even to a logged-in admin who otherwise has access
        // to the table.
        TEST(GetTableRowsEndpointTest, GetTableRowsPeopleHidesPasswordHash) {
            Auth::ServerConfig::Shutdown();
            Auth::ServerConfig::InitializeTestMode();

            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction(
                "GetTableRowsPeopleHidesPasswordHash",
                [&](Transaction& transaction) {

                // Insert the (people, password_hash) redaction BEFORE
                // constructing EndpointTestHelper (the helper
                // snapshots the table contents into the WebApp at
                // construction time).
                TableHelpers::AdminColumnRedactions redactions(
                    testDatabaseUtil.GetDatabaseHelper());
                redactions.AddAdminColumnRedaction(
                    transaction,
                    DbSchema::kPeopleTable,
                    DbSchema::kPeoplePasswordHash);

                EndpointTestHelper endpointHelper(transaction, testDatabaseUtil);

                endpointHelper.AddAdminTopLevelTable(
                    transaction, DbSchema::kPeopleTable);

                auto secrets = endpointHelper.GetSecretsHelper();
                secrets->AddSecret(transaction,
                    Secrets::kAuthSessionMaxDuractioninMicros,
                    std::to_string(10LL * 60LL * 1000000LL));

                Auth::PersonHelper personHelper(testDatabaseUtil.GetDatabaseHelper());
                Auth::PersonInfo info{
                    "admin-rows@example.com", "Admin", "Rows" };
                personHelper.CreateFullyValidatedUser(
                    transaction, info, "Password!");

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

                auto cookieMgr = endpointHelper.GetCookieManagerTest();
                Auth::CookieProperties cp;
                cp.path = "/";
                cp.sameSite = Auth::CookieSameSitePolicy::None;
                cookieMgr->SetCookie("session_token", sessionToken, cp);

                crow::request req;
                crow::response resp;
                req.url = "/api/get_table_rows/people";
                endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);
                ASSERT_TRUE(body.HasChild("sortedColumnNames", nullptr));

                for (const auto& col : body["sortedColumnNames"].GetArray()) {
                    EXPECT_NE(col.Get<std::string>(), "password_hash")
                        << "password_hash leaked through "
                           "/api/get_table_rows/people";
                }
                auto columnPresent = [&](std::string_view name) {
                    for (const auto& col : body["sortedColumnNames"].GetArray()) {
                        if (col.Get<std::string>() == name) return true;
                    }
                    return false;
                };
                EXPECT_TRUE(columnPresent("email"));
                EXPECT_TRUE(columnPresent("first_name"));

                // Belt-and-suspenders: scan every cell for an Argon2
                // hash prefix. CreateFullyValidatedUser produces a
                // hash that begins with "$argon2".
                for (const auto& row : body["dataTable"].GetArray()) {
                    for (const auto& cell : row.GetArray()) {
                        std::string s = cell.Is<std::string>()
                            ? cell.Get<std::string>()
                            : cell.ToSimpleString();
                        EXPECT_EQ(s.find("$argon2"), std::string::npos)
                            << "Argon2 hash leaked through generic CRUD response";
                    }
                }
            });

            Auth::ServerConfig::Shutdown();
        }

    } // namespace
}  // namespace Endpoints
