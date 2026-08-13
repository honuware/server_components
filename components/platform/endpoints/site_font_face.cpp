#include "site_font_face.h"

#include <string>

#include "endpoints/endpoint_auth_helper.h"
#include "sql_util/table_helpers/site_fonts.h"
#include "db_schema/site_fonts.h"
#include "util/error_response.h"
#include "util/types.h"

namespace Endpoints {
namespace {

// The format recorded at upload time (from magic bytes) decides the MIME type.
// An unrecognised value is refused rather than guessed: serving unknown bytes
// under a font/* type is exactly what nosniff exists to prevent, and guessing
// would undo it.
std::string ContentTypeForFormat(std::string_view format) {
    if (format == "woff2") return "font/woff2";
    if (format == "woff") return "font/woff";
    if (format == "ttf") return "font/ttf";
    if (format == "otf") return "font/otf";
    return "";
}

void HandleGet(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    int64_t faceId) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetSiteFontFace(endpointAuthHelper, req, resp, faceId);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/site_font_face/<int>")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp,
                    int64_t faceId) {
                    HandleGet(webApp, req, resp, faceId);
                });
    }
} g_setupRouting;

}  // namespace

void GetSiteFontFace(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp,
    int64_t faceId) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    std::string contentType;
    std::string bytes;
    tp->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::SiteFonts fonts(endpointAuthHelper.GetDatabaseHelper());
        KeyValueTable face = fonts.GetFace(transaction, faceId);
        if (face.empty()) {
            return;
        }
        auto it = face.find(std::string(DbSchema::kSiteFontFaceFormat));
        if (it == face.end()) {
            return;
        }
        contentType = ContentTypeForFormat(it->second);
        if (contentType.empty()) {
            return;
        }
        bytes = fonts.GetFaceBytes(transaction, faceId);
    });

    if (contentType.empty() || bytes.empty()) {
        resp = ErrorResponse::NotFound("Font face not found");
        return;
    }

    resp.code = 200;
    resp.set_header("Content-Type", contentType);
    // Uploaded bytes must never be reinterpretable as script from our origin.
    resp.set_header("X-Content-Type-Options", "nosniff");
    // A face is immutable — a replacement upload gets a new id — so it can be
    // cached hard. CloudFront caches this exactly like a photo.
    resp.set_header("Cache-Control", "public, max-age=31536000, immutable");
    resp.body.assign(bytes.begin(), bytes.end());
}

}  // namespace Endpoints
