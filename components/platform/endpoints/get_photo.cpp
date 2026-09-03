#include "get_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

void HandleGet(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& tableName,
    int64_t tableItemId) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetPhoto(endpointAuthHelper, req, resp, tableName, tableItemId);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/get_photo/<string>/<int>")
            .methods(crow::HTTPMethod::GET)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& tableName, int64_t tableItemId) {
                    HandleGet(webApp, req, resp, tableName, tableItemId);
                });
    }
} g_setupRouting;

}  // namespace

void GetPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId) {

    // Phase 3.7 of the security review: previously required only
    // IsLoggedIn, which meant any authenticated customer could fetch
    // any image keyed by (table, item_id). Now we additionally require
    // that the active user has read access to `tableName` via the
    // standard generic-CRUD allow-list — admins see all admin tables,
    // permission holders see their permission's tables, anonymous
    // users see nothing through this endpoint (use home_page_photos
    // for the public path).
    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return;
    }

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    tp->RunInTransaction([&](Transaction& transaction) {
        if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
            resp = ErrorResponse::ValidationError(
                "Table(" + std::string(tableName) + ") is not an allowed table.");
            return;
        }

        Images::ImageHelper imageHelper(endpointAuthHelper.GetDatabaseHelper());
        auto photoData = imageHelper.GetSourcePhotoData(
            transaction, tableName, tableItemId);

        if (!photoData.has_value()) {
            resp = ErrorResponse::NotFound("No photo found for this item");
            return;
        }

        // Same validator the scaled endpoint uses: the source photo's
        // last-updated stamp, which changes when the image is replaced.
        const std::string etag =
            "\"" + std::to_string(photoData->lastUpdatedAtUs) + "\"";
        const std::string ifNoneMatch =
            std::string(req.get_header_value("If-None-Match"));
        if (!ifNoneMatch.empty() && ifNoneMatch == etag) {
            resp.code = 304;
            return;
        }

        resp.code = 200;
        resp.set_header("Content-Type",
            Images::ImageMimeType(photoData->type));
        // Polish Phase 11.1 — see the note in get_scaled_photo.cpp: an
        // uploaded SVG is served from our own origin and is never sanitised,
        // so it must only ever be rendered passively. `nosniff` stops a
        // browser re-deciding the type of what we hand it.
        resp.set_header("X-Content-Type-Options", "nosniff");
        // `private, no-cache`, matching the AUTHENTICATED branch of
        // get_scaled_photo — and note this endpoint is authenticated for
        // EVERY table it serves (see the login + IsTableAllowed checks
        // above).
        //
        // It used to send `public, max-age=86400`. Both halves were wrong:
        //
        // - `public` invites shared caches to store a response that is
        //   gated on the caller's permissions. That is precisely the
        //   cross-user leak the scaled endpoint's comment guards against —
        //   CloudFront could hand one user's profile photo to another.
        // - `max-age=86400` meant a replaced image stayed stale for a DAY
        //   with no way for the studio to force it.
        //
        // `private` keeps shared caches out; `no-cache` still lets the
        // user's own browser store the bytes and revalidate cheaply
        // against the ETag above.
        resp.set_header("Cache-Control", "private, no-cache");
        resp.set_header("ETag", etag);
        resp.body.assign(photoData->bytes.begin(), photoData->bytes.end());
    });
}

}  // namespace Endpoints
