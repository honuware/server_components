#include "session.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/table_helpers/people.h"
#include "sql_util/table_helpers/sessions.h"
#include "db_schema/device_tokens.h"
#include "endpoints/endpoint_test_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "test/src/util/database_test_helper.h"
#include "business_logic/auth/cookie_manager_test_util.h"
#include "business_logic/auth/server_config.h"
#include "business_logic/auth/person.h"

// Helpers for roles/permissions/assignments
#include "sql_util/table_helpers/roles.h"
#include "sql_util/table_helpers/permissions.h"
#include "sql_util/table_helpers/role_permissions.h"
#include "sql_util/table_helpers/role_assignments.h"
#include "db_schema/roles.h"

namespace Auth {
namespace {

static int64_t AddTestPerson(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    std::string_view email) {
    // Use TestDatabaseUtil::AddPerson instead of TableHelpers::People
    return testDb.AddPerson(transaction, email, "First", "Last", "pwdhash");
}

TEST(SessionTest, InitializeFromLoginBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromLoginBasic", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL)); // 30 minutes
        // Provide device token secret (not used since remember=false)
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(60LL * 60LL * 1000000LL)); // 60 minutes

        int64_t personId = AddTestPerson(transaction, testDb, "user@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "user@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());

        std::string token;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "user@example.com", token));
        ASSERT_FALSE(token.empty());

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();

        Session session(testDb.GetDatabaseHelper());
        session.InitializeFromLogin(transaction, secrets, token, cookieMgr, "user@example.com", false);

        EXPECT_TRUE(session.IsLoggedIn());
        EXPECT_EQ(session.GetPersonId(), personId);

        // Validate cookie properties
        auto propsMap = cookieMgr->GetCookieProperties();
        auto it = propsMap.find("session_token");
        ASSERT_NE(it, propsMap.end());
        EXPECT_EQ(it->second.path, "/");
        EXPECT_EQ(it->second.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_TRUE(it->second.httpOnly);
        EXPECT_FALSE(it->second.secure);
        EXPECT_EQ(it->second.maxAgeInMicros, 30LL * 60LL * 1000000LL);

        // Ensure no device token row was added
        int deviceTokenCount = std::stoi(
            transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM device_tokens WHERE person_id = $1",
                std::to_string(personId)));
        EXPECT_EQ(deviceTokenCount, 0);
    });
    ServerConfig::Shutdown();
}

// Phase 1.5 / 1.6 contract: in production mode, the session cookie is emitted
// with `Secure`, the configured `Domain`, and `SameSite=Lax`. SameSite=Lax is
// the right choice because the AWS deploy is same-origin behind CloudFront
// (S3 + EC2 fronted by one CloudFront distribution), so the browser never
// sends a cross-site cookie and we don't need the `SameSite=None; Secure`
// dance that cross-origin frontends require. Locks the same-origin cookie
// contract so a future cross-origin migration must be deliberate.
TEST(SessionTest, InitializeFromLoginProdModeCookieHasSecureAndDomain) {
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "InitializeFromLoginProdMode",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();

        secrets->AddSecret(transaction, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(transaction, Secrets::kWebsiteAddress, "knottyyoga.com");
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(60LL * 60LL * 1000000LL));

        ServerConfig::Initialize(
            transaction, secrets, &endpointHelper.GetWebApp());
        ASSERT_TRUE(ServerConfig::GetInstance().IsProdMode());
        ASSERT_FALSE(ServerConfig::GetInstance().IsTestMode());

        AddTestPerson(transaction, testDb, "prod_cookie@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "prod_cookie@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string token;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, "prod_cookie@example.com", token));
        ASSERT_FALSE(token.empty());

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();

        Session session(testDb.GetDatabaseHelper());
        session.InitializeFromLogin(
            transaction, secrets, token, cookieMgr,
            "prod_cookie@example.com", /*remember=*/false);

        EXPECT_TRUE(session.IsLoggedIn());

        auto propsMap = cookieMgr->GetCookieProperties();
        auto it = propsMap.find("session_token");
        ASSERT_NE(it, propsMap.end());
        EXPECT_EQ(it->second.path, "/");
        EXPECT_TRUE(it->second.httpOnly);
        EXPECT_TRUE(it->second.secure)
            << "Prod-mode session cookies must be Secure so the browser only "
               "sends them over HTTPS";
        EXPECT_EQ(it->second.domain, "knottyyoga.com");
        EXPECT_EQ(it->second.sameSite, CookieSameSitePolicy::Lax)
            << "Same-origin behind CloudFront uses SameSite=Lax — flipping to "
               "None would force matching Secure-only cross-origin handling";
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromLoginRemember) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromLoginRemember", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL)); // 30 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days

        int64_t personId = AddTestPerson(transaction, testDb, "remember@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "remember@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string token;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "remember@example.com", token));
        ASSERT_FALSE(token.empty());

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();

        Session session(testDb.GetDatabaseHelper());
        session.InitializeFromLogin(transaction, secrets, token, cookieMgr, "remember@example.com", true);

        EXPECT_TRUE(session.IsLoggedIn());
        EXPECT_EQ(session.GetPersonId(), personId);

        // Session cookie exists
        auto propsMap = cookieMgr->GetCookieProperties();
        auto itSession = propsMap.find("session_token");
        ASSERT_NE(itSession, propsMap.end());

        // Device token cookie should also exist
        auto itDevice = propsMap.find("device_token");
        ASSERT_NE(itDevice, propsMap.end());
        EXPECT_EQ(itDevice->second.path, "/");
        EXPECT_EQ(itDevice->second.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_TRUE(itDevice->second.httpOnly);
        EXPECT_FALSE(itDevice->second.secure);
        EXPECT_GT(itDevice->second.maxAgeInMicros, itSession->second.maxAgeInMicros); // typically longer

        // Verify device_tokens row for personId
        int deviceTokenCount = std::stoi(
            transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM device_tokens WHERE person_id = $1",
                std::to_string(personId)));
        EXPECT_EQ(deviceTokenCount, 1);
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromLoginNotFound) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromLoginNotFound", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        EXPECT_THROW(
            session.InitializeFromLogin(transaction, secrets, "invalid-token", cookieMgr, "user@example.com", false),
            std::runtime_error);
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromFromCookieBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromFromCookieBasic", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(10LL * 60LL * 1000000LL));

        int64_t personId = AddTestPerson(transaction, testDb, "cookieuser@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "cookieuser@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string token;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "cookieuser@example.com", token));

        // Put token into test cookie manager
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", token, cp);

        Session session(testDb.GetDatabaseHelper());
        bool ok = session.InitializeFromFromCookie(transaction, cookieMgr);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(session.IsLoggedIn());
        EXPECT_EQ(session.GetPersonId(), personId);
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromFromCookieNotPresent) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromFromCookieNotPresent", [&](Transaction& transaction) {
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        EXPECT_FALSE(session.InitializeFromFromCookie(transaction, cookieMgr));
        EXPECT_FALSE(session.IsLoggedIn());
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromFromCookieInvalid) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromFromCookieInvalid", [&](Transaction& transaction) {
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cookieMgr->SetCookie("session_token", "bad-token", cp);
        Session session(testDb.GetDatabaseHelper());
        EXPECT_FALSE(session.InitializeFromFromCookie(transaction, cookieMgr));
        EXPECT_FALSE(session.IsLoggedIn());
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromDeviceTokenBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromDeviceTokenBasic", [&](Transaction& transaction) {
        // Secrets for session and device token durations
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL)); // 30 minutes
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL)); // 7 days

        // Create validated person
        int64_t personId = AddTestPerson(transaction, testDb, "devtoken@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "devtoken@example.com");

        // Create initial device token
        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(personHelper.CreateDeviceToken(transaction, secrets, "devtoken@example.com", tokenId, base64Secret));
        ASSERT_FALSE(tokenId.empty());
        ASSERT_FALSE(base64Secret.empty());

        // Confirm DB row exists and capture initial expires_at
        KeyValueTable rowBefore = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", tokenId);
        ASSERT_FALSE(rowBefore.empty());
        EXPECT_EQ(rowBefore.at(std::string(DbSchema::kDeviceTokensPersonId)), std::to_string(personId));
        int64_t expiresBefore = std::stoll(rowBefore.at(std::string(DbSchema::kDeviceTokensExpiresAt)));

        // Seed cookie with combined device token: "<uuid>.<base64Secret>"
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        const std::string combinedToken = tokenId + "." + base64Secret;
        cookieMgr->SetCookie("device_token", combinedToken, cp);

        // Call InitializeFromDeviceToken (should rotate device token and set a session cookie)
        Session session(testDb.GetDatabaseHelper());
        bool ok = session.InitializeFromDeviceToken(transaction, secrets, cookieMgr);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(session.IsLoggedIn());
        EXPECT_EQ(session.GetPersonId(), personId);

        // Validate session cookie is present
        auto propsMap = cookieMgr->GetCookieProperties();
        auto cookies = cookieMgr->GetCookies();
        auto itSess = cookies.find("session_token");
        ASSERT_NE(itSess, cookies.end());
        auto itSessProp = propsMap.find("session_token");
        ASSERT_NE(itSessProp, propsMap.end());
        EXPECT_EQ(itSessProp->second.path, "/");
        EXPECT_EQ(itSessProp->second.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_TRUE(itSessProp->second.httpOnly);

        // Validate device token cookie is present AND secret changed (rotated)
        auto itDevCombined = cookies.find("device_token");
        ASSERT_NE(itDevCombined, cookies.end());
        // Parse "<uuid>.<base64Secret>"
        std::string newCombined = itDevCombined->second;
        auto [newUuid, newSecret] = SplitString(newCombined, ".");
        EXPECT_NE(std::string(newUuid), tokenId);           // token id rotated
        EXPECT_NE(std::string(newSecret), base64Secret);    // secret rotated

        // Validate DB updated: expires_at should be >= previous and secret_hash changed
        KeyValueTable rowAfter = transaction.RunSqlStatementReturningOneRow(
            "SELECT * FROM device_tokens WHERE uuid = $1", newUuid);
        ASSERT_FALSE(rowAfter.empty());
        int64_t expiresAfter = std::stoll(rowAfter.at(std::string(DbSchema::kDeviceTokensExpiresAt)));
        EXPECT_GE(expiresAfter, expiresBefore);
        // Cannot directly compare hash, but ensure row still for same person and not revoked
        EXPECT_EQ(rowAfter.at(std::string(DbSchema::kDeviceTokensPersonId)), std::to_string(personId));
        EXPECT_TRUE(rowAfter.at(std::string(DbSchema::kDeviceTokensRevoked)) == "f" ||
                    rowAfter.at(std::string(DbSchema::kDeviceTokensRevoked)) == "false" ||
                    rowAfter.at(std::string(DbSchema::kDeviceTokensRevoked)) == "0");
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromDeviceTokenNotFound) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromDeviceTokenNotFound", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        // Seed cookie with a non-existent combined token
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("device_token", "00000000-0000-0000-0000-000000000000.ZmFrZVNlY3JldA==", cp); // "fakeSecret"

        Session session(testDb.GetDatabaseHelper());
        EXPECT_FALSE(session.InitializeFromDeviceToken(transaction, secrets, cookieMgr));
        EXPECT_FALSE(session.IsLoggedIn());

        // No session cookie should be set
        auto cookies = cookieMgr->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromDeviceTokenExpired) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromDeviceTokenExpired", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(5LL * 60LL * 1000000LL)); // 5 minutes

        int64_t personId = AddTestPerson(transaction, testDb, "expired@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "expired@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(personHelper.CreateDeviceToken(transaction, secrets, "expired@example.com", tokenId, base64Secret));

        // Force expiration in the past
        transaction.RunSqlStatement("UPDATE device_tokens SET expires_at = 1 WHERE uuid = $1", tokenId);

        // Seed cookie with current token id and secret combined
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("device_token", tokenId + "." + base64Secret, cp);

        Session session(testDb.GetDatabaseHelper());
        EXPECT_FALSE(session.InitializeFromDeviceToken(transaction, secrets, cookieMgr));
        EXPECT_FALSE(session.IsLoggedIn());

        auto cookies = cookieMgr->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromDeviceTokenRevoked) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeFromDeviceTokenRevoked", [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(5LL * 60LL * 1000000LL));

        int64_t personId = AddTestPerson(transaction, testDb, "revoked@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "revoked@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(personHelper.CreateDeviceToken(transaction, secrets, "revoked@example.com", tokenId, base64Secret));

        // Revoke token
        transaction.RunSqlStatement("UPDATE device_tokens SET revoked = TRUE WHERE uuid = $1", tokenId);

        // Cookies: combined device token
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("device_token", tokenId + "." + base64Secret, cp);

        Session session(testDb.GetDatabaseHelper());
        EXPECT_FALSE(session.InitializeFromDeviceToken(transaction, secrets, cookieMgr));
        EXPECT_FALSE(session.IsLoggedIn());

        auto cookies = cookieMgr->GetCookies();
        EXPECT_TRUE(cookies.find("session_token") == cookies.end());
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, IsAdminTrue) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAdminTrue", [&](Transaction& transaction) {
        int64_t personId = AddTestPerson(transaction, testDb, "adminuser@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "adminuser@example.com");

        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "adminuser@example.com", sessionToken));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp; cp.path = "/"; cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        Session session(testDb.GetDatabaseHelper());
        ASSERT_TRUE(session.InitializeFromFromCookie(transaction, cookieMgr));
        ASSERT_TRUE(session.IsLoggedIn());

        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t ridAdmin = roles.AddRole(transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, ridAdmin);

        EXPECT_TRUE(session.IsAdmin(transaction));
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, IsAdminFalse) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("IsAdminFalse", [&](Transaction& transaction) {
        int64_t personId = AddTestPerson(transaction, testDb, "regularuser@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "regularuser@example.com");

        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "regularuser@example.com", sessionToken));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp; cp.path = "/"; cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        Session session(testDb.GetDatabaseHelper());
        ASSERT_TRUE(session.InitializeFromFromCookie(transaction, cookieMgr));
        ASSERT_TRUE(session.IsLoggedIn());

        // Create the admin role but do NOT assign it to this person
        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        roles.AddRole(transaction, std::string(DbSchema::kRoleNameAdmin), "Administrator");

        EXPECT_FALSE(session.IsAdmin(transaction));
    });

    ServerConfig::Shutdown();
}

// Updated calls to include Transaction& parameter

TEST(SessionTest, ActiveUserHasRoleBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ActiveUserHasRoleBasic", [&](Transaction& transaction) {
        int64_t personId = AddTestPerson(transaction, testDb, "roleuser@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "roleuser@example.com");

        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "roleuser@example.com", sessionToken));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp; cp.path = "/"; cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        Session session(testDb.GetDatabaseHelper());
        ASSERT_TRUE(session.InitializeFromFromCookie(transaction, cookieMgr));
        ASSERT_TRUE(session.IsLoggedIn());
        ASSERT_EQ(session.GetPersonId(), personId);

        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t ridAdmin = roles.AddRole(transaction, "admin", "Administrator");

        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, ridAdmin);

        EXPECT_TRUE(session.ActiveUserHasRole(transaction, "admin"));
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, ActiveUserHasRoleNotFound) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ActiveUserHasRoleNotFound", [&](Transaction& transaction) {
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        EXPECT_THROW(session.ActiveUserHasRole(transaction, "anything"), std::runtime_error);
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, ActiveUserHasPermissionBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ActiveUserHasPermissionBasic", [&](Transaction& transaction) {
        int64_t personId = AddTestPerson(transaction, testDb, "permuser@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "permuser@example.com");

        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(15LL * 60LL * 1000000LL));

        PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(transaction, secrets, "permuser@example.com", sessionToken));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp; cp.path = "/"; cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie("session_token", sessionToken, cp);

        Session session(testDb.GetDatabaseHelper());
        ASSERT_TRUE(session.InitializeFromFromCookie(transaction, cookieMgr));
        ASSERT_TRUE(session.IsLoggedIn());
        ASSERT_EQ(session.GetPersonId(), personId);

        TableHelpers::Roles roles(testDb.GetDatabaseHelper());
        int64_t ridViewer = roles.AddRole(transaction, "viewer", "Viewer");
        int64_t ridEditor = roles.AddRole(transaction, "editor", "Editor");

        TableHelpers::Permissions perms(testDb.GetDatabaseHelper());
        int64_t pidRead = perms.AddPermission(transaction, "read", "Read");
        int64_t pidWrite = perms.AddPermission(transaction, "write", "Write");

        TableHelpers::RolePermissions rperms(testDb.GetDatabaseHelper());
        rperms.AddRolePermission(transaction, ridViewer, pidRead);
        rperms.AddRolePermission(transaction, ridEditor, pidWrite);

        TableHelpers::RoleAssignments ra(testDb.GetDatabaseHelper());
        ra.AddRoleAssignment(transaction, personId, ridViewer);
        ra.AddRoleAssignment(transaction, personId, ridEditor);

        EXPECT_TRUE(session.ActiveUserHasPermission(transaction, "read"));
        EXPECT_TRUE(session.ActiveUserHasPermission(transaction, "write"));
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, ActiveUserHasPermissionNotFound) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ActiveUserHasPermissionNotFound", [&](Transaction& transaction) {
        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        EXPECT_THROW(session.ActiveUserHasPermission(transaction, "anyperm"), std::runtime_error);
    });

    ServerConfig::Shutdown();
}

// =====================================================================
// Phase 4.1 of the security review: companion CSRF cookie.
//
// Every successful login (and every device-token reauthentication)
// emits a `csrft` cookie alongside the session cookie. The cookie is:
//   - 32 bytes of random material, base64-encoded
//   - NOT HttpOnly (the SPA must read it via document.cookie to echo
//     it back as the X-CSRF-Token header)
//   - SameSite=Lax (matches the session cookie — same-origin behind
//     CloudFront)
//   - Secure + Domain set in production (mirrors the session cookie)
//   - Same Max-Age as the session cookie so the pair expires together
//
// Session::GetCsrfToken() exposes the freshly-generated value so
// tests downstream of the login can build matching X-CSRF-Token
// requests. Production code never needs to read it server-side; the
// SPA reads the cookie value directly.
// =====================================================================

TEST(SessionTest, InitializeFromLoginSetsCsrfTokenCookie) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "InitializeFromLoginSetsCsrfTokenCookie",
        [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(60LL * 60LL * 1000000LL));

        AddTestPerson(transaction, testDb, "csrf-login@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "csrf-login@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string sessionToken;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, "csrf-login@example.com", sessionToken));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        session.InitializeFromLogin(
            transaction, secrets, sessionToken, cookieMgr,
            "csrf-login@example.com", false);

        // The csrft cookie was set with the right shape.
        auto cookies = cookieMgr->GetCookies();
        auto itCsrf = cookies.find("csrft");
        ASSERT_NE(itCsrf, cookies.end()) << "csrft cookie missing after login";
        EXPECT_FALSE(itCsrf->second.empty())
            << "csrft cookie value must not be empty";
        // Distinct from the session cookie — they're independent draws.
        auto itSess = cookies.find("session_token");
        ASSERT_NE(itSess, cookies.end());
        EXPECT_NE(itCsrf->second, itSess->second);

        // Session::GetCsrfToken() returns the same value.
        EXPECT_EQ(session.GetCsrfToken(), itCsrf->second);

        // Cookie attributes.
        auto propsMap = cookieMgr->GetCookieProperties();
        auto itProps = propsMap.find("csrft");
        ASSERT_NE(itProps, propsMap.end());
        EXPECT_EQ(itProps->second.path, "/");
        EXPECT_EQ(itProps->second.sameSite, CookieSameSitePolicy::Lax);
        EXPECT_FALSE(itProps->second.httpOnly)
            << "csrft cookie must be readable by the SPA — HttpOnly=false";
        EXPECT_FALSE(itProps->second.secure)
            << "test mode is not prod — Secure should be false";
        EXPECT_EQ(itProps->second.maxAgeInMicros, 30LL * 60LL * 1000000LL)
            << "csrft cookie should expire with the session cookie";
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromLoginCsrfTokenIsFreshOnEveryCall) {
    // Two consecutive logins must produce different csrft values —
    // the token is freshly generated each time, never reused.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "InitializeFromLoginCsrfTokenIsFreshOnEveryCall",
        [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(60LL * 60LL * 1000000LL));

        AddTestPerson(transaction, testDb, "csrf-fresh@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "csrf-fresh@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());

        std::string token1;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, "csrf-fresh@example.com", token1));
        auto cookieMgr1 = Auth::Test::MakeCookieManagerTest();
        Session session1(testDb.GetDatabaseHelper());
        session1.InitializeFromLogin(
            transaction, secrets, token1, cookieMgr1,
            "csrf-fresh@example.com", false);

        std::string token2;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, "csrf-fresh@example.com", token2));
        auto cookieMgr2 = Auth::Test::MakeCookieManagerTest();
        Session session2(testDb.GetDatabaseHelper());
        session2.InitializeFromLogin(
            transaction, secrets, token2, cookieMgr2,
            "csrf-fresh@example.com", false);

        EXPECT_FALSE(session1.GetCsrfToken().empty());
        EXPECT_FALSE(session2.GetCsrfToken().empty());
        EXPECT_NE(session1.GetCsrfToken(), session2.GetCsrfToken())
            << "every login must mint a fresh CSRF token";
    });
    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromLoginProdModeCsrfTokenCookieHasSecureAndDomain) {
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "InitializeFromLoginProdModeCsrfTokenCookieHasSecureAndDomain",
        [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();

        secrets->AddSecret(transaction, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(transaction, Secrets::kWebsiteAddress, "knottyyoga.com");
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(60LL * 60LL * 1000000LL));

        ServerConfig::Initialize(
            transaction, secrets, &endpointHelper.GetWebApp());
        ASSERT_TRUE(ServerConfig::GetInstance().IsProdMode());

        AddTestPerson(transaction, testDb, "csrf-prod@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "csrf-prod@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string token;
        ASSERT_TRUE(personHelper.CreateSessionToken(
            transaction, secrets, "csrf-prod@example.com", token));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        Session session(testDb.GetDatabaseHelper());
        session.InitializeFromLogin(
            transaction, secrets, token, cookieMgr,
            "csrf-prod@example.com", false);

        auto propsMap = cookieMgr->GetCookieProperties();
        auto it = propsMap.find("csrft");
        ASSERT_NE(it, propsMap.end());
        EXPECT_FALSE(it->second.httpOnly)
            << "SPA must be able to read the csrft cookie";
        EXPECT_TRUE(it->second.secure)
            << "Prod mode must mark csrft Secure (HTTPS-only)";
        EXPECT_EQ(it->second.domain, "knottyyoga.com");
        EXPECT_EQ(it->second.sameSite, CookieSameSitePolicy::Lax);
    });

    ServerConfig::Shutdown();
}

TEST(SessionTest, InitializeFromDeviceTokenSetsCsrfTokenCookie) {
    // Reauthenticating via the long-lived device token also mints a
    // fresh csrft cookie. Without this, the SPA would have an active
    // session cookie but no CSRF cookie to read, and every
    // state-changing request would be 403'd by CsrfGuard.
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "InitializeFromDeviceTokenSetsCsrfTokenCookie",
        [&](Transaction& transaction) {
        auto secrets = Secrets::Test::MakeTestSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAuthSessionMaxDuractioninMicros,
            std::to_string(30LL * 60LL * 1000000LL));
        secrets->AddSecret(transaction, Secrets::kAuthDeviceTokenMaxDurationInMicros,
            std::to_string(7LL * 24LL * 60LL * 60LL * 1000000LL));

        AddTestPerson(transaction, testDb, "csrf-dev@example.com");
        TableHelpers::People people(testDb.GetDatabaseHelper());
        people.VerifyPersonEmail(transaction, "csrf-dev@example.com");

        Auth::PersonHelper personHelper(testDb.GetDatabaseHelper());
        std::string tokenId;
        std::string base64Secret;
        ASSERT_TRUE(personHelper.CreateDeviceToken(
            transaction, secrets, "csrf-dev@example.com", tokenId, base64Secret));

        auto cookieMgr = Auth::Test::MakeCookieManagerTest();
        CookieProperties cp;
        cp.path = "/";
        cp.sameSite = CookieSameSitePolicy::None;
        cookieMgr->SetCookie(
            "device_token", tokenId + "." + base64Secret, cp);

        Session session(testDb.GetDatabaseHelper());
        bool ok = session.InitializeFromDeviceToken(
            transaction, secrets, cookieMgr);
        ASSERT_TRUE(ok);

        auto cookies = cookieMgr->GetCookies();
        auto itCsrf = cookies.find("csrft");
        ASSERT_NE(itCsrf, cookies.end())
            << "device-token reauth must also mint a csrft cookie";
        EXPECT_FALSE(itCsrf->second.empty());
        EXPECT_EQ(session.GetCsrfToken(), itCsrf->second);

        auto propsMap = cookieMgr->GetCookieProperties();
        auto itProps = propsMap.find("csrft");
        ASSERT_NE(itProps, propsMap.end());
        EXPECT_FALSE(itProps->second.httpOnly);
        EXPECT_EQ(itProps->second.sameSite, CookieSameSitePolicy::Lax);
    });
    ServerConfig::Shutdown();
}

} // namespace
} // namespace Auth