#include "get_scaled_photo.h"

#include "endpoints/endpoint_auth_helper.h"
#include "endpoints/public_photo_tables.h"
#include "business_logic/auth/session.h"
#include "business_logic/images/image_helper.h"
#include "util/secrets/secret_keys.h"
#include "util/bounding_rect.h"
#include "util/error_response.h"

namespace Endpoints {
namespace {

// Phase 3.7 of the security review + H8 extraction: which tables' scaled photos
// anonymous visitors may read (bypassing login + IsTableAllowed, served with
// public cache headers) is app configuration, not framework policy. The consuming
// app registers its public-imagery tables via the Endpoints::PublicPhotoTables
// service on the WebApp; when none is registered nothing is public and every
// scaled-photo read requires login. This keeps the framework endpoint app-agnostic.
// (knottyyoga registers {home_page_photos, classes, instructors} — the public
// carousel + marketing imagery that must render for anonymous visitors.)

void HandleGet(
    WebApp* webApp,
    const crow::request& req,
    crow::response& resp,
    const std::string& tableName,
    int64_t tableItemId,
    int width,
    int height) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetScaledPhoto(
            endpointAuthHelper, req, resp,
            tableName, tableItemId, width, height);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(
            webApp->GetApp(),
            "/api/get_scaled_photo/<string>/<int>/<int>/<int>")
            .methods(crow::HTTPMethod::GET)(
                [=](const crow::request& req, crow::response& resp,
                    const std::string& tableName, int64_t tableItemId,
                    int width, int height) {
                    HandleGet(webApp, req, resp,
                        tableName, tableItemId, width, height);
                });
    }
} g_setupRouting;

}  // namespace

void GetScaledPhoto(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    std::string_view tableName,
    int64_t tableItemId,
    int width,
    int height) {

    if (width <= 0 || height <= 0) {
        resp = ErrorResponse::BadRequest(
            "Width and height must be positive");
        return;
    }

    // The app-configured public-read table set (empty => nothing public).
    auto publicPhotoTables = endpointAuthHelper.GetService<PublicPhotoTables>();
    const bool isPublicTable =
        publicPhotoTables && publicPhotoTables->Contains(tableName);

    // Phase 3.7 of the security review: require login and that the
    // active user can read the requested table — EXCEPT for tables on
    // the public-scaled-photo allow-list (`home_page_photos`), which
    // the SPA fetches unauthenticated for the landing-page carousel.
    // The dedicated `/api/home_page_photos/<count>` endpoint returns
    // the carousel METADATA (ids, titles); the IMAGE BYTES come back
    // through here, so this endpoint must permit anonymous reads for
    // those tables or the public site shows a wall of broken images.
    //
    // The dimension clamp below still applies — an attacker can't
    // request unbounded sizes via the public path.
    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!isPublicTable && !session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return;
    }

    auto transactionProvider = endpointAuthHelper.GetTransactionProvider();
    if (!transactionProvider) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    Secrets::SecretsHelperPtr secretsHelper =
        endpointAuthHelper.GetSecretsHelper();
    DatabaseHelper databaseHelper = endpointAuthHelper.GetDatabaseHelper();

    transactionProvider->RunInTransaction([&](Transaction& transaction) {
        // IsTableAllowed runs for the AUTHENTICATED path — it's the
        // table-probing defense (a logged-in non-admin shouldn't be
        // able to fetch scaled photos for, say, `payments`). For the
        // PUBLIC path, `IsPublicScaledPhotoTable` is itself the
        // allow-list — we already decided which tables anonymous
        // callers may read, and the base `allowed_tables` table is
        // populated narrowly (`classes`, `products`) so the
        // unauthenticated branch would otherwise reject
        // `home_page_photos` here even though we already permitted
        // it above. Bypass IsTableAllowed for the public allow-list
        // entries; keep it for everything else.
        if (!isPublicTable &&
            !endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
            resp = ErrorResponse::ValidationError(
                "Table(" + std::string(tableName) + ") is not an allowed table.");
            return;
        }

        // Clamp dimensions to max public scaled size
        if (secretsHelper) {
            std::string maxWidthStr = secretsHelper->LookupSecret(
                transaction, Secrets::kImageMaxPublicScaledWidth);
            std::string maxHeightStr = secretsHelper->LookupSecret(
                transaction, Secrets::kImageMaxPublicScaledHeight);
            int maxWidth = 800;
            int maxHeight = 800;
            if (!maxWidthStr.empty()) {
                maxWidth = std::stoi(maxWidthStr);
            }
            if (!maxHeightStr.empty()) {
                maxHeight = std::stoi(maxHeightStr);
            }
            if (width > maxWidth || height > maxHeight) {
                BoundingRect::Rect inputRect{width, height};
                BoundingRect::Rect boundingRect{maxWidth, maxHeight};
                BoundingRect::Rect clipped =
                    BoundingRect::GetClippedRect(inputRect, boundingRect);
                width = clipped.width;
                height = clipped.height;
            }
        }

        Images::ImageHelper imageHelper(databaseHelper, secretsHelper);
        auto scaledResult = imageHelper.GetScaledPhotoForItem(
            transaction, tableName, tableItemId, width, height);

        if (!scaledResult.success) {
            resp = ErrorResponse::NotFound(scaledResult.errorMessage);
            return;
        }

        // ETag based on source photo's last update timestamp
        std::string etag = "\"" +
            std::to_string(scaledResult.sourceLastUpdatedAtUs) + "\"";

        // Check If-None-Match for conditional GET
        std::string ifNoneMatch = std::string(req.get_header_value("If-None-Match"));
        if (!ifNoneMatch.empty() && ifNoneMatch == etag) {
            resp.code = 304;
            return;
        }

        resp.code = 200;
        resp.set_header("Content-Type",
            "image/" + scaledResult.photo.type);
        // Cache policy splits on the public/authenticated path:
        //
        // - PUBLIC path (`home_page_photos` and any future entry in
        //   `IsPublicScaledPhotoTable`): the response is fine for any
        //   anonymous viewer to see, so we permit CloudFront to serve
        //   it directly from the edge. `public, max-age=3600` means
        //   the edge AND the browser cache for an hour without hitting
        //   the origin; a stale carousel image for up to one hour is
        //   the right tradeoff against the public-site traffic the
        //   landing page sees. The ETag is still emitted so a
        //   conditional revalidation (browser or CloudFront origin
        //   fetch after max-age expires) cheaply returns 304 instead
        //   of re-sending the image bytes. When an admin needs a
        //   faster propagation than one hour (rare — replacing a
        //   carousel photo) they can invalidate the CloudFront
        //   distribution.
        //
        // - AUTHENTICATED path (everything else): the response may
        //   contain user-specific imagery (profile photos, etc.).
        //   `private` is load-bearing — it forbids shared caches
        //   (CloudFront) from storing the response at all, so a
        //   future config drift in CloudFront's cache-key behavior
        //   can't accidentally serve user A's profile photo to user
        //   B. `no-cache` still lets the BROWSER store it
        //   (revalidating on each use), which gives the user's own
        //   tabs free reuse without leaking across users.
        if (isPublicTable) {
            resp.set_header("Cache-Control", "public, max-age=3600");
        } else {
            resp.set_header("Cache-Control", "private, no-cache");
        }
        resp.set_header("ETag", etag);
        resp.body.assign(
            scaledResult.photo.bytes.begin(),
            scaledResult.photo.bytes.end());
    });
}

}  // namespace Endpoints
