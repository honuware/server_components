#include "site_font_face.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "db_schema/site_fonts.h"
#include "endpoints/endpoint_test_helper.h"
#include "endpoints/web_app.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "test/src/util/database_test_helper.h"

namespace Endpoints {
namespace {

int64_t SeedFace(
    Transaction& transaction,
    TestDatabaseUtil& testDb,
    std::string_view format,
    std::string_view bytes) {
    TableHelpers::SiteFonts fonts(testDb.GetDatabaseHelper());
    int64_t font = fonts.AddFont(
        transaction, "Studio Sans", "sans-serif",
        DbSchema::kSiteFontSourceKindUploaded, 0, "", 10);
    return fonts.AddFace(transaction, font, 400, "normal", format, bytes);
}

TEST(SiteFontFaceEndpointTest, ServesTheBytesWithTheRecordedFontMimeType) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("ServesFontBytes", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        std::string bytes("wOF2\x00\x01binary\x00tail", 18);
        int64_t faceId = SeedFace(transaction, testDb, "woff2", bytes);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_font_face/" + std::to_string(faceId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 200);
        EXPECT_EQ(resp.get_header_value("Content-Type"), "font/woff2");
        // The body must survive its NULs — a truncated font renders nothing.
        EXPECT_EQ(resp.body.size(), bytes.size());
        EXPECT_EQ(resp.body, bytes);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteFontFaceEndpointTest, SendsNosniffSoUploadedBytesCannotBecomeScript) {
    // The security-relevant assertion of the whole upload feature: a tenant's
    // bytes are served from our origin, so the browser must never be allowed to
    // sniff them into something executable.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontNosniff", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t faceId = SeedFace(transaction, testDb, "woff2", "wOF2bytes");

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_font_face/" + std::to_string(faceId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.get_header_value("X-Content-Type-Options"), "nosniff");
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteFontFaceEndpointTest, CachesHardBecauseAFaceIsImmutable) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontCache", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t faceId = SeedFace(transaction, testDb, "woff2", "wOF2bytes");

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_font_face/" + std::to_string(faceId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.get_header_value("Cache-Control"),
                  "public, max-age=31536000, immutable");
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteFontFaceEndpointTest, MapsEachStoredFormatToItsOwnMimeType) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    const struct { const char* format; const char* mime; } kCases[] = {
        {"woff2", "font/woff2"},
        {"woff", "font/woff"},
        {"ttf", "font/ttf"},
        {"otf", "font/otf"},
    };

    for (const auto& testCase : kCases) {
        TestDatabaseUtil testDb;
        testDb.RunInTransaction("FontMime", [&](Transaction& transaction) {
            EndpointTestHelper endpointHelper(transaction, testDb);
            int64_t faceId = SeedFace(
                transaction, testDb, testCase.format, "wOF2bytes");

            crow::request req;
            req.method = crow::HTTPMethod::Get;
            req.url = "/api/site_font_face/" + std::to_string(faceId);
            crow::response resp;
            endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

            EXPECT_EQ(resp.get_header_value("Content-Type"), testCase.mime)
                << "format: " << testCase.format;
        });
    }

    Auth::ServerConfig::Shutdown();
}

TEST(SiteFontFaceEndpointTest, RefusesAFaceWhoseStoredFormatIsUnrecognised) {
    // Serving unknown bytes under a font/* type is what nosniff exists to
    // prevent; guessing a type would undo it.
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontBadFormat", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);
        int64_t faceId = SeedFace(transaction, testDb, "exe", "MZbytes");

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_font_face/" + std::to_string(faceId);
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 404);
    });

    Auth::ServerConfig::Shutdown();
}

TEST(SiteFontFaceEndpointTest, IsPublicButUnknownIdsAre404) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();

    TestDatabaseUtil testDb;
    testDb.RunInTransaction("FontMissing", [&](Transaction& transaction) {
        EndpointTestHelper endpointHelper(transaction, testDb);

        crow::request req;
        req.method = crow::HTTPMethod::Get;
        req.url = "/api/site_font_face/999999";
        // Deliberately no auth cookie — a webfont is fetched with no credentials.
        crow::response resp;
        endpointHelper.GetWebApp().GetApp().handle_full(req, resp);

        EXPECT_EQ(resp.code, 404);
    });

    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
