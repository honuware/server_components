#include "manage_site_theme_bundle.h"

#include <string>

#include "business_logic/auth/session.h"
#include "business_logic/branding/theme_bundle_export.h"
#include "business_logic/branding/theme_bundle_import.h"
#include "business_logic/branding/theme_bundle_json.h"
#include "business_logic/branding/theme_bundle_assets.h"
#include "business_logic/branding/theme_bundle_zip.h"
#include "endpoints/endpoint_auth_helper.h"
#include "util/date_time_util.h"
#include "util/env.h"
#include "util/error_response.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper.h"
#include "util/types.h"

namespace Endpoints {
namespace {

bool RequireAdmin(
    EndpointAuthHelper& endpointAuthHelper,
    Transaction& transaction,
    crow::response& resp) {
    Auth::Session& session = endpointAuthHelper.GetSession();
    if (!session.IsLoggedIn()) {
        resp = ErrorResponse::NotAuthenticated("Login required");
        return false;
    }
    if (!session.IsAdmin(transaction)) {
        resp = ErrorResponse::NotAuthorized("Admin required");
        return false;
    }
    return true;
}

// GetEnvWithFallback returns NULLPTR when neither variable is set, and
// constructing a std::string from a null pointer is undefined behaviour — it
// segfaults. Every one of these is unset in a test environment, so the crash is
// not a rare edge: it is the default.
std::string EnvOrEmpty(const char* primary, const char* fallback) {
    const char* value = Util::GetEnvWithFallback(primary, fallback);
    return value ? std::string(value) : std::string();
}

bool FlagIsSet(const crow::request& req, const char* name) {
    const char* value = req.url_params.get(name);
    if (!value) {
        return false;
    }
    const std::string text(value);
    return text.empty() || text == "1" || text == "true";
}

Branding::ThemeBundleImportOptions OptionsFrom(const crow::request& req) {
    Branding::ThemeBundleImportOptions options;
    // LENIENT by default.
    //
    // This was Strict, on the reasoning that a typo'd token silently doing
    // nothing is the worst available outcome. In practice the outcome that
    // actually happened was worse: one unrecognised key from a slightly
    // different build refused the entire file, so a studio could not restore
    // their own theme and had nothing to act on but "that theme file has
    // settings this site does not have".
    //
    // Nothing is silent either way — every skipped item is on the report and
    // rendered in the editor. Lenient applies what it understands and says what
    // it did not; `?strict=1` is the opt-in for all-or-nothing.
    options.strictness = FlagIsSet(req, "strict")
                             ? Branding::BundleStrictness::Strict
                             : Branding::BundleStrictness::Lenient;
    // Replace by default (OQ-TF2).
    options.merge = FlagIsSet(req, "merge");
    return options;
}

// Shared by both POST routes — they differ only in whether they write.
Json::Value RunImport(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp,
    bool dryRun) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return {};
    }

    // A refused import has to ROLL BACK, and RunInTransaction commits whenever
    // its lambda returns normally — it only rolls back on an exception. The
    // import writes the framework half (secrets, fonts) before handing control
    // to an app section, so a section that refuses would otherwise leave those
    // writes committed while the response said the import failed: half a theme,
    // and the studio told it got none.
    //
    // So the failure path throws, and the report is carried out on the
    // exception rather than assigned across the boundary.
    struct ImportRefused : std::runtime_error {
        Branding::BundleImportReport report;
        explicit ImportRefused(Branding::BundleImportReport r)
            : std::runtime_error(r.error), report(std::move(r)) {}
    };

    Json::Value result;
    try {
        tp->RunInTransaction([&](Transaction& transaction) {
            if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
                return;
            }
            if (req.body.empty()) {
                resp = ErrorResponse::BadRequest("No theme file in the request.");
                return;
            }

            Json::Value json;
            std::map<std::string, std::string> assets;
            const std::string zipError =
                Branding::ThemeBundleFromZip(req.body, json, assets);
            if (!zipError.empty()) {
                // A studio's mis-typed or truncated file is a 400, never a 500.
                // Nothing has been written yet, so returning is safe here.
                resp = ErrorResponse::BadRequest(zipError);
                return;
            }

            Branding::ThemeBundleImportOptions options = OptionsFrom(req);
            options.dryRun = dryRun;
            Branding::BundleImportReport report = Branding::ImportThemeBundleJson(
                endpointAuthHelper.GetDatabaseHelper(), transaction,
                *endpointAuthHelper.GetSecretsHelper(), json, assets, options);

            if (!report.ok) {
                throw ImportRefused(std::move(report));
            }
            result = Branding::BundleImportReportToJson(report);
            resp.code = 200;
        });
    }
    catch (const ImportRefused& refused) {
        // The report travels WITH the refusal: the interesting part of a
        // rejection is which keys or sections caused it, and a bare message
        // would throw that away.
        result = Branding::BundleImportReportToJson(refused.report);
        resp = ErrorResponse::ValidationError(refused.report.error);
        resp.set_header("Content-Type", "application/json");
        resp.write(result.ToString());
        resp.code = 400;
    }
    catch (const std::exception& error) {
        // Anything the import did not anticipate — most often a database that
        // does not have a table this build expects.
        //
        // This used to escape to the route handler and become a bare 500
        // ("An unexpected error occurred"), with the real cause visible only in
        // the server log. An admin importing their own theme was told nothing
        // and had nowhere to look. The reason is now in the response, framed as
        // what it is: the theme was not applied, and here is what went wrong.
        //
        // Safe to include: this route is admin-only, and the alternative is an
        // administrator debugging their own site by reading a server log.
        Branding::BundleImportReport report;
        report.ok = false;
        report.error =
            std::string("The theme file could not be applied: ") + error.what();
        result = Branding::BundleImportReportToJson(report);
        resp = ErrorResponse::InternalError(report.error);
        resp.set_header("Content-Type", "application/json");
        resp.write(result.ToString());
        resp.code = 500;
    }

    return result;
}

void HandleGet(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        GetManageSiteThemeBundle(endpointAuthHelper, req, resp);
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandlePostValidate(
    WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result =
            PostManageSiteThemeBundleValidate(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
    try {
        EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
        endpointAuthHelper.Initialize();
        Json::Value result = PostManageSiteThemeBundle(endpointAuthHelper, req, resp);
        if (resp.code == 200) {
            resp.set_header("Content-Type", "application/json");
            resp.write(result.ToString());
        }
    }
    catch (std::exception& e) {
        resp = ErrorResponse::InternalError(e.what());
    }
    resp.end();
}

class SetupRouting : public RoutingBase {
public:
    void AddRoute(WebApp* webApp) override {
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_theme_bundle")
            .methods(crow::HTTPMethod::Get)(
                [=](const crow::request& req, crow::response& resp) {
                    HandleGet(webApp, req, resp);
                });
        // The dry run is its own route rather than a flag on the apply route:
        // "tell me what this would do" and "do it" should not be one character
        // apart in a URL.
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_theme_bundle/validate")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePostValidate(webApp, req, resp);
                });
        CROW_ROUTE(webApp->GetApp(), "/api/manage/site_theme_bundle")
            .methods(crow::HTTPMethod::Post)(
                [=](const crow::request& req, crow::response& resp) {
                    HandlePost(webApp, req, resp);
                });
    }
} g_setupRouting;

}  // namespace

void GetManageSiteThemeBundle(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request&,
    crow::response& resp) {

    auto tp = endpointAuthHelper.GetTransactionProvider();
    if (!tp) {
        resp = ErrorResponse::InternalError("Database unavailable");
        return;
    }

    tp->RunInTransaction([&](Transaction& transaction) {
        if (!RequireAdmin(endpointAuthHelper, transaction, resp)) {
            return;
        }
        Secrets::SecretsHelperPtr secrets = endpointAuthHelper.GetSecretsHelper();

        Branding::ThemeBundleExportOptions options;
        // The studio's own name, so a downloaded file is recognisable in a
        // folder of them.
        options.name = secrets->LookupSecret(transaction, Secrets::kMailSenderName);
        // Date only, from the database's clock (the same one every row's
        // created_at_us comes from). `exported_at` is provenance a human reads,
        // and a second-resolution stamp would make two otherwise identical
        // exports differ for no reason a reviewer cares about.
        options.exportedAt = DateTimeUtil::FormatDateFromMicroseconds(
            std::atoll(
                transaction.RunSqlStatementReturningOneValue("SELECT now_us()")
                    .c_str()));
        options.app = EnvOrEmpty("HONUWARE_APP_NAME", "");
        options.site = secrets->LookupSecret(transaction, Secrets::kWebsiteAddress);
        options.honuwareVersion =
            EnvOrEmpty("HONUWARE_VERSION", "KNOTTYYOGA_VERSION");

        Branding::ThemeBundle bundle;
        const std::string reason = Branding::ExportThemeBundle(
            endpointAuthHelper.GetDatabaseHelper(), transaction, *secrets,
            options, bundle);
        if (!reason.empty()) {
            resp = ErrorResponse::InternalError(reason);
            return;
        }

        const std::string zip = Branding::ThemeBundleToZip(bundle);
        if (zip.empty()) {
            // We produced this bundle, so a failure here is ours.
            resp = ErrorResponse::InternalError("The theme file could not be built.");
            return;
        }

        std::string stem = Branding::SanitizeAssetStem(options.name);
        if (stem.empty()) {
            stem = "theme";
        }
        resp.code = 200;
        resp.set_header("Content-Type", "application/zip");
        resp.set_header("Content-Disposition",
                        "attachment; filename=\"" + stem + "-theme.zip\"");
        // A theme is per-tenant AND changes whenever the studio edits it.
        resp.set_header("Cache-Control", "no-store");
        resp.write(zip);
    });
}

Json::Value PostManageSiteThemeBundleValidate(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {
    return RunImport(endpointAuthHelper, req, resp, /*dryRun=*/true);
}

Json::Value PostManageSiteThemeBundle(
    EndpointAuthHelper& endpointAuthHelper,
    const crow::request& req,
    crow::response& resp) {
    return RunImport(endpointAuthHelper, req, resp, /*dryRun=*/false);
}

}  // namespace Endpoints
