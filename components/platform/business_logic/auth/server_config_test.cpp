#include "server_config.h"

#include <cstdlib>

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <crow/common.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "util/secrets/secrets_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "test/src/util/database_test_helper.h"
#include "endpoints/endpoint_test_helper.h"

namespace Auth {
namespace {

// RAII guard for KNOTTYYOGA_DEV_CORS_ORIGIN. Mirrors the
// OriginSecretEnvScope helper in cloudfront_origin_guard_test.cpp —
// scrubbed on entry and exit so leaked env state can't bleed between
// tests.
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

class DevCorsOriginEnvScope {
public:
    DevCorsOriginEnvScope() { ScrubAll(); }
    ~DevCorsOriginEnvScope() { ScrubAll(); }
private:
    static void ScrubAll() {
        // Phase 1.1 rename: scrub BOTH names so the legacy fallback can't
        // pull a value from the developer's launch environment.
        UnsetEnv("HONUWARE_DEV_CORS_ORIGIN");
        UnsetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN");
    }
};

// Phase 12.1: RAII guard that scrubs every env var the prod-validator
// inspects, on both entry and exit. ValidateProdEnvironment reads
// global process state via getenv — bleed between tests would make
// failures order-dependent. The scope ALSO covers the dev-cors var
// so the existing CORS tests downstream don't get surprised by a
// leak from a 12.1 test that came earlier.
class ProdValidationEnvScope {
public:
    ProdValidationEnvScope() { ScrubAll(); }
    ~ProdValidationEnvScope() { ScrubAll(); }
private:
    static void ScrubAll() {
        // Phase 1.1 rename: scrub BOTH the canonical HONUWARE_* and the
        // legacy KNOTTYYOGA_* names — the validator reads the canonical
        // name first and falls back to the legacy one, so a leak on
        // either would perturb order-dependent prod-validation tests.
        UnsetEnv("HONUWARE_ORIGIN_SECRET");
        UnsetEnv("KNOTTYYOGA_ORIGIN_SECRET");
        UnsetEnv("HONUWARE_TRUST_PROXY");
        UnsetEnv("KNOTTYYOGA_TRUST_PROXY");
        UnsetEnv("HONUWARE_SECRET_KEY");
        UnsetEnv("KNOTTYYOGA_SECRET_KEY");
        UnsetEnv("HONUWARE_DEV_CORS_ORIGIN");
        UnsetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN");
    }
};

TEST(ServerConfigTest, InitializeProdMode) {
    ServerConfig::Shutdown();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeProdMode", [&](Transaction& transaction) {
        EndpointTestHelper endpointTestHelper(transaction, testDb);
        auto secrets = endpointTestHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kServerProductionMode, "true");
        ServerConfig::Initialize(transaction, secrets, &endpointTestHelper.GetWebApp());
        EXPECT_TRUE(ServerConfig::GetInstance().IsProdMode());
        EXPECT_FALSE(ServerConfig::GetInstance().IsTestMode());
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, InitializeDevMode) {
    ServerConfig::Shutdown();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("InitializeDevMode", [&](Transaction& transaction) {
        EndpointTestHelper endpointTestHelper(transaction, testDb);
        auto secrets = endpointTestHelper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(transaction, secrets, &endpointTestHelper.GetWebApp());
        EXPECT_FALSE(ServerConfig::GetInstance().IsProdMode());
        EXPECT_FALSE(ServerConfig::GetInstance().IsTestMode());
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, InitializeTestModeBasic) {
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();
    EXPECT_FALSE(ServerConfig::GetInstance().IsProdMode());
    EXPECT_TRUE(ServerConfig::GetInstance().IsTestMode());
    ServerConfig::Shutdown();
}

// =====================================================================
// Phase 7.2 of the security review: dev-mode CORS opt-in via
// KNOTTYYOGA_DEV_CORS_ORIGIN.
//
// CORSHandler stores its rules in private state — to verify the
// configured origin is actually applied, we issue a request through
// the full middleware chain and inspect the
// Access-Control-Allow-Origin response header.
// =====================================================================

// CORS rule inspection helper. Crow's `handle_full` is a debug-only
// shortcut that does NOT run middleware after_handle, so we can't
// assert on response headers from a routed request. Instead we
// invoke the configured CORSHandler directly on a synthesized
// request/response pair and inspect what it would emit. This still
// goes through the same `apply()` code path Crow uses on real
// connections.
crow::response InvokeCorsAfterHandle(
    EndpointTestHelper& endpointHelper,
    crow::HTTPMethod method,
    const std::string& url,
    const std::string& originHeader) {
    crow::request req;
    req.method = method;
    req.url = url;
    if (!originHeader.empty()) req.add_header("Origin", originHeader);
    crow::response res;
    auto& cors = endpointHelper.GetWebApp().GetApp()
        .get_middleware<crow::CORSHandler>();
    crow::CORSHandler::context ctx;
    cors.after_handle(req, res, ctx);
    return res;
}

TEST(ServerConfigTest, DevCorsOriginUnsetLeavesCorsAtDefault) {
    DevCorsOriginEnvScope envScope;  // env var unset
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevCorsOriginUnsetLeavesCorsAtDefault",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "http://localhost:4200");

        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Origin")), "*")
            << "dev mode without KNOTTYYOGA_DEV_CORS_ORIGIN should leave "
               "CORS at its default permissive setting";
        // Default rules don't allow credentials.
        EXPECT_TRUE(resp.get_header_value("Access-Control-Allow-Credentials").empty());
    });

    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, DevCorsOriginSetRegistersThatOrigin) {
    DevCorsOriginEnvScope envScope;
    SetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN", "http://localhost:4200");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevCorsOriginSetRegistersThatOrigin",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "http://localhost:4200");

        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Origin")),
                  "http://localhost:4200")
            << "dev mode with KNOTTYYOGA_DEV_CORS_ORIGIN should pin "
               "Access-Control-Allow-Origin to exactly that origin";
        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Credentials")),
                  "true")
            << "dev CORS opt-in must include allow_credentials so the "
               "SPA's cookie-bearing requests work";
    });

    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, DevCorsOriginCanonicalHonuwareNameRegistersThatOrigin) {
    // Phase 1.1 rename: the canonical HONUWARE_DEV_CORS_ORIGIN works on
    // its own, with no legacy var set.
    DevCorsOriginEnvScope envScope;
    SetEnv("HONUWARE_DEV_CORS_ORIGIN", "http://localhost:4200");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "DevCorsOriginCanonicalHonuwareNameRegistersThatOrigin",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "http://localhost:4200");

        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Origin")),
                  "http://localhost:4200");
        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Credentials")),
                  "true");
    });

    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, DevCorsOriginHonuwareNameTakesPrecedenceOverLegacy) {
    // When both are set, the canonical name wins.
    DevCorsOriginEnvScope envScope;
    SetEnv("HONUWARE_DEV_CORS_ORIGIN", "http://canonical.example");
    SetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN", "http://legacy.example");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "DevCorsOriginHonuwareNameTakesPrecedenceOverLegacy",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "http://canonical.example");

        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Origin")),
                  "http://canonical.example")
            << "canonical HONUWARE name must win over the legacy KNOTTYYOGA name";
    });

    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, DevCorsOriginEmptyValueIsTreatedAsUnset) {
    // Defensive: an empty env-var value (e.g., set to "" via shell)
    // should NOT register an empty-origin CORS rule.
    DevCorsOriginEnvScope envScope;
    SetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN", "");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevCorsOriginEmptyValueIsTreatedAsUnset",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "http://localhost:4200");

        EXPECT_EQ(std::string(resp.get_header_value("Access-Control-Allow-Origin")), "*");
    });

    ServerConfig::Shutdown();
}

TEST(ServerConfigTest, DevCorsOriginIgnoredInProdMode) {
    // Even with the env var set, prod mode pins to kWebsiteAddress.
    DevCorsOriginEnvScope envScope;
    SetEnv("KNOTTYYOGA_DEV_CORS_ORIGIN", "http://attacker.example");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevCorsOriginIgnoredInProdMode",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "https://knottyyoga.example");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        crow::response resp = InvokeCorsAfterHandle(
            endpointHelper, crow::HTTPMethod::Get, "/api/health",
            "https://knottyyoga.example");

        std::string origin = std::string(
            resp.get_header_value("Access-Control-Allow-Origin"));
        EXPECT_EQ(origin, "https://knottyyoga.example")
            << "prod must use kWebsiteAddress, never the dev env var";
        EXPECT_NE(origin, "http://attacker.example");
    });

    ServerConfig::Shutdown();
}

// =====================================================================
// Phase 12.1 of the security review: prod-mode startup validation.
//
// ValidateProdEnvironment fails loud when prod mode is on but one of
// the required guard env vars / secrets is missing. In dev/test mode
// it's a silent no-op — local development must not require operators
// to set the prod-only env vars.
// =====================================================================

namespace {

// Convenience: stamp every env var the validator inspects to a known-
// good value AND set kWebsiteAddress in the test secrets. Each
// individual-missing test then unsets / clears exactly one of them.
void SetAllProdGuardsValid(
    Transaction& tx,
    Secrets::Test::SecretsHelperTestPtr secrets) {
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "test-origin-secret");
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    SetEnv("KNOTTYYOGA_SECRET_KEY",
           // 32 bytes base64 — content doesn't matter for the
           // validator (it only checks set-and-non-empty); we never
           // actually decode it here.
           "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    secrets->AddSecret(tx, Secrets::kWebsiteAddress,
                       "https://knottyyoga.example");
}

}  // namespace

TEST(ServerConfigProdValidationTest, DevModeIsAlwaysAnNoOp) {
    // The validator is a no-op when ServerConfig is in dev mode,
    // regardless of which env vars are set. This is load-bearing —
    // local devs must not need to export prod-only env vars to run
    // the server locally.
    ProdValidationEnvScope envScope;  // all unset
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevModeIsAlwaysAnNoOp", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_NO_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets))
            << "validator must be a silent no-op in dev mode";
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, TestModeIsAlwaysAnNoOp) {
    // Same contract for the explicit test-mode init path. Tests that
    // use ServerConfig::InitializeTestMode() must never have to
    // export real env vars.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("TestModeIsAlwaysAnNoOp", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        EXPECT_NO_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets));
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdAllSetPasses) {
    // Happy path: all four required items configured → validator
    // returns silently. This is the deploy-is-healthy case.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdAllSetPasses", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_NO_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets));
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdCanonicalHonuwareNamesPass) {
    // Phase 1.1 rename: the canonical HONUWARE_* env vars satisfy the
    // validator on their own — the legacy KNOTTYYOGA_* names are only a
    // fallback, not a requirement.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdCanonicalHonuwareNamesPass", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetEnv("HONUWARE_ORIGIN_SECRET", "test-origin-secret");
        SetEnv("HONUWARE_TRUST_PROXY", "1");
        SetEnv("HONUWARE_SECRET_KEY",
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress,
                           "https://knottyyoga.example");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_NO_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets));
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdMissingOriginSecretThrows) {
    // Phase 1.7 guard: without the origin secret, CloudFront-
    // bypass requests are not rejected. Refuse to boot.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdMissingOriginSecretThrows", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        UnsetEnv("KNOTTYYOGA_ORIGIN_SECRET");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        try {
            ServerConfig::ValidateProdEnvironment(tx, secrets);
            FAIL() << "expected ValidateProdEnvironment to throw";
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            EXPECT_NE(msg.find("HONUWARE_ORIGIN_SECRET"), std::string::npos)
                << "error message must name the missing item: " << msg;
        }
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdEmptyOriginSecretIsTreatedAsMissing) {
    // Defensive: `export KNOTTYYOGA_ORIGIN_SECRET=""` must not be
    // accepted as "configured" — the guard reads it as effectively
    // unset (see cloudfront_origin_guard.cpp's empty-string check).
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdEmptyOriginSecretIsTreatedAsMissing",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "");  // empty
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets),
            std::runtime_error);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdMissingTrustProxyThrows) {
    // Phase 5.5: without trust-proxy, ResolveClientIp returns the
    // CloudFront edge IP for every request and the per-IP rate
    // limit collapses to one bucket.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdMissingTrustProxyThrows", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        UnsetEnv("KNOTTYYOGA_TRUST_PROXY");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        try {
            ServerConfig::ValidateProdEnvironment(tx, secrets);
            FAIL() << "expected throw";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("HONUWARE_TRUST_PROXY"),
                      std::string::npos);
        }
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdTrustProxyFalseValueIsTreatedAsMissing) {
    // proxy_trust.cpp accepts only "1"/"true" (case-insensitive).
    // "0", "false", and unrecognized strings are rejected. The
    // validator must match — silently treating "0" as a valid setting
    // would defeat the guard.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ProdTrustProxyFalseValueIsTreatedAsMissing",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        SetEnv("KNOTTYYOGA_TRUST_PROXY", "0");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets),
            std::runtime_error);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdMissingSecretKeyThrows) {
    // Phase 8.1: without the master key, SecretsAtRest falls back
    // to plaintext storage of config_secrets values. SecretsAtRest
    // itself fails loud in prod, but the validator double-checks
    // here so an operator sees the full missing-items list in one
    // error.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdMissingSecretKeyThrows", [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        SetAllProdGuardsValid(tx, secrets);
        UnsetEnv("KNOTTYYOGA_SECRET_KEY");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        try {
            ServerConfig::ValidateProdEnvironment(tx, secrets);
            FAIL() << "expected throw";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("HONUWARE_SECRET_KEY"),
                      std::string::npos);
        }
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdMissingWebsiteAddressThrows) {
    // kWebsiteAddress drives the prod CORS pin AND verification-
    // email link bases. Empty → CORS rejects all browser requests
    // AND verification mails go out with broken URLs.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdMissingWebsiteAddressThrows",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        // Stamp the env vars but leave kWebsiteAddress empty by
        // calling AddSecret with "" (which overrides the test
        // helper's preloaded default of `http://localhost:18080/`).
        SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "x");
        SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
        SetEnv("KNOTTYYOGA_SECRET_KEY", "y");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        try {
            ServerConfig::ValidateProdEnvironment(tx, secrets);
            FAIL() << "expected throw";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find("website_address"),
                      std::string::npos);
        }
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdAllMissingListsEveryFailureAtOnce) {
    // The whole point of building a list (vs throwing on the first
    // problem) is to let an operator fix every misconfiguration in
    // ONE redeploy. Verify the error message names all four items
    // when every guard is missing.
    ProdValidationEnvScope envScope;  // env unset
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdAllMissingListsEveryFailureAtOnce",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        secrets->AddSecret(tx, Secrets::kWebsiteAddress, "");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        try {
            ServerConfig::ValidateProdEnvironment(tx, secrets);
            FAIL() << "expected throw";
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            EXPECT_NE(msg.find("HONUWARE_ORIGIN_SECRET"), std::string::npos)
                << msg;
            EXPECT_NE(msg.find("HONUWARE_TRUST_PROXY"), std::string::npos)
                << msg;
            EXPECT_NE(msg.find("HONUWARE_SECRET_KEY"), std::string::npos)
                << msg;
            EXPECT_NE(msg.find("website_address"), std::string::npos)
                << msg;
        }
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigProdValidationTest, ProdThrowsBeforeAcceptingConnections) {
    // The contract is: validator must propagate the exception so
    // main.cpp doesn't reach the .run() call. We can't run main()
    // from a test, but we can verify the exception type and that
    // the validator throws (not swallows). main.cpp's call site
    // just lets the throw escape RunInTransaction; the test mirrors
    // the same expectation.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdThrowsBeforeAcceptingConnections",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        EXPECT_THROW(
            ServerConfig::ValidateProdEnvironment(tx, secrets),
            std::runtime_error);
    });
    ServerConfig::Shutdown();
}

// =====================================================================
// Phase 12.2 of the security review: BuildSecurityPostureSummary.
//
// One-line startup summary the operator sees in the deploy logs.
// The format is the public contract — tests pin the exact keys + the
// set/unset semantics so a future refactor can't silently drop a flag.
// =====================================================================

TEST(ServerConfigSecurityPostureTest, ProdAllGuardsSetReportsAllSet) {
    ProdValidationEnvScope envScope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "x");
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "1");
    SetEnv("KNOTTYYOGA_SECRET_KEY", "y");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProdAllGuardsSetReportsAllSet",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "true");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        std::string summary = ServerConfig::BuildSecurityPostureSummary();
        // Pinned format — if you change the key names below, also
        // update the regex any operator dashboard / Splunk extract
        // is using to parse this line.
        EXPECT_NE(summary.find("[security_posture]"), std::string::npos)
            << "summary must start with the log-prefix tag for grep";
        EXPECT_NE(summary.find("prod=true"), std::string::npos);
        EXPECT_NE(summary.find("csrf_guard=on"), std::string::npos);
        EXPECT_NE(summary.find("origin_secret=set"), std::string::npos);
        EXPECT_NE(summary.find("proxy_trust=set"), std::string::npos);
        EXPECT_NE(summary.find("secret_key=set"), std::string::npos);
        EXPECT_NE(summary.find("security_headers=on"), std::string::npos);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigSecurityPostureTest, DevAllGuardsUnsetReportsAllUnset) {
    // Dev-mode default: no env vars set, summary should NOT lie about
    // the guards being configured. This is the "I'm a local dev, why
    // does my server log say origin_secret=set?" test — if the
    // implementation hardcoded "set" instead of reading the env, this
    // would catch it.
    ProdValidationEnvScope envScope;  // all unset
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DevAllGuardsUnsetReportsAllUnset",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        std::string summary = ServerConfig::BuildSecurityPostureSummary();
        EXPECT_NE(summary.find("prod=false"), std::string::npos);
        EXPECT_NE(summary.find("origin_secret=unset"), std::string::npos);
        EXPECT_NE(summary.find("proxy_trust=unset"), std::string::npos);
        EXPECT_NE(summary.find("secret_key=unset"), std::string::npos);
        // The unconditional middleware flags should still report on.
        EXPECT_NE(summary.find("csrf_guard=on"), std::string::npos);
        EXPECT_NE(summary.find("security_headers=on"), std::string::npos);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigSecurityPostureTest,
     ProxyTrustEmptyStringReportsUnsetNotSet) {
    // Defensive: `KNOTTYYOGA_TRUST_PROXY=""` is the same effective
    // state as unset, and the summary must say so. Operator who's
    // staring at `proxy_trust=set` will assume the guard works;
    // misreporting would cause silent rate-limit collapse behind
    // CloudFront (see Phase 5.5).
    ProdValidationEnvScope envScope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction(
        "ProxyTrustEmptyStringReportsUnsetNotSet",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        std::string summary = ServerConfig::BuildSecurityPostureSummary();
        EXPECT_NE(summary.find("proxy_trust=unset"), std::string::npos)
            << "empty value must report unset, not set";
        EXPECT_EQ(summary.find("proxy_trust=set"), std::string::npos)
            << "must not contain 'proxy_trust=set' anywhere";
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigSecurityPostureTest,
     ProxyTrustZeroValueReportsUnsetNotSet) {
    // proxy_trust.cpp interprets "0" / "false" as untrusted. The
    // summary must match — reporting "set" for a value that doesn't
    // actually enable trust would mislead operators.
    ProdValidationEnvScope envScope;
    SetEnv("KNOTTYYOGA_TRUST_PROXY", "0");
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ProxyTrustZeroValueReportsUnsetNotSet",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        std::string summary = ServerConfig::BuildSecurityPostureSummary();
        EXPECT_NE(summary.find("proxy_trust=unset"), std::string::npos);
        EXPECT_EQ(summary.find("proxy_trust=set"), std::string::npos);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigSecurityPostureTest, PartialSetReportsAccurately) {
    // Mixed configuration — only origin_secret and secret_key set —
    // catches a bug where the implementation might've ANDed all the
    // flags into a single boolean.
    ProdValidationEnvScope envScope;
    SetEnv("KNOTTYYOGA_ORIGIN_SECRET", "x");
    SetEnv("KNOTTYYOGA_SECRET_KEY", "y");
    // KNOTTYYOGA_TRUST_PROXY left unset
    ServerConfig::Shutdown();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("PartialSetReportsAccurately",
        [&](Transaction& tx) {
        EndpointTestHelper endpointHelper(tx, testDb);
        auto secrets = endpointHelper.GetSecretsHelper();
        secrets->AddSecret(tx, Secrets::kServerProductionMode, "false");
        ServerConfig::Initialize(tx, secrets, &endpointHelper.GetWebApp());

        std::string summary = ServerConfig::BuildSecurityPostureSummary();
        EXPECT_NE(summary.find("origin_secret=set"), std::string::npos);
        EXPECT_NE(summary.find("secret_key=set"), std::string::npos);
        EXPECT_NE(summary.find("proxy_trust=unset"), std::string::npos);
    });
    ServerConfig::Shutdown();
}

TEST(ServerConfigSecurityPostureTest, TestModeReportsProdFalse) {
    // ServerConfig::InitializeTestMode() sets prodMode_=false. The
    // summary should reflect that, since tests don't run with prod
    // semantics.
    ProdValidationEnvScope envScope;
    ServerConfig::Shutdown();
    ServerConfig::InitializeTestMode();

    std::string summary = ServerConfig::BuildSecurityPostureSummary();
    EXPECT_NE(summary.find("prod=false"), std::string::npos);

    ServerConfig::Shutdown();
}

} // namespace
} // namespace Auth