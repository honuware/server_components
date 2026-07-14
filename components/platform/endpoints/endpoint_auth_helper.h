#pragma once

#include <crow.h>
#include "web_app.h"
#include "business_logic/auth/session.h"

namespace Auth {

    class CookieManager;
    using CookieManagerPtr = std::shared_ptr<CookieManager>;

}

// Helper class for endpoint authentication related utilities.
// (Initial implementation minimal; can be expanded later.)
class EndpointAuthHelper {
public:
    EndpointAuthHelper(
        WebApp& app,
        const crow::request& req,
        crow::response& res);
    EndpointAuthHelper(const EndpointAuthHelper&) = delete;
    EndpointAuthHelper& operator=(const EndpointAuthHelper&) = delete;
    ~EndpointAuthHelper() = default;

    void Initialize(); // Placeholder

    // Accessors to underlying objects
    WebApp& App();
    const crow::request& Request() const;
    crow::response& Response();
    Auth::Session& GetSession();

    // Convenience accessors delegating to WebApp
    DbSchema::DatabaseInfo GetDatabaseInfo() const;
    Secrets::SecretsHelperPtr GetSecretsHelper() const;
    Mail::MailHelperPtr GetMailHelper() const;
    TransactionProviderPtr GetTransactionProvider() const;
    DatabaseHelper GetDatabaseHelper() const;
    Auth::CookieManagerPtr GetCookieManager() const;
    // Phase 3.8 of the security review: see WebApp::GetColumnRedactions.
    const TableHelpers::ColumnRedactionSet& GetColumnRedactions() const;

    // Componentization Phase 1.6: generic passthrough to WebApp's app-service
    // registry. The framework façade stays app-agnostic; app-specific typed
    // accessors (e.g. GetSquareClient()) live on the app-derived
    // AppEndpointAuthHelper, which is implemented on top of this. Returns
    // nullptr when no service of type T was registered.
    template <typename T>
    std::shared_ptr<T> GetService() const {
        return app_.GetService<T>();
    }

    // Database helpers
    const std::string& GetDatabaseName() const;
    StringArray GetAllowedTables(Transaction& transaction);
    bool IsTableAllowed(
        Transaction& transaction, std::string_view tableName);

    // Phase 3.6 of the security review: convenience gate for endpoints
    // that need a specific permission (e.g. /api/staff/* requires
    // `staff_access`). Returns true on allow. On false, writes the
    // appropriate error to the response and the caller should bail.
    // The two-step response matters: 401 if not logged in (SPA
    // redirects to login); 403 if logged in but missing the
    // permission.
    bool RequirePermission(
        Transaction& transaction,
        std::string_view permissionName,
        crow::response& resp);

private:
    WebApp& app_;
    const crow::request& request_;
    crow::response& response_;
    Auth::Session session_;
    Auth::CookieManagerPtr cookieManager_;
};