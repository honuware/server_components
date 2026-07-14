#pragma once

#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <crow.h>
#include <crow/middlewares/cors.h>
#include <crow/middlewares/cookie_parser.h>

#include "endpoints/web_app_types.h"
#include "endpoints/cloudfront_origin_guard.h"
#include "endpoints/csrf_guard.h"
#include "endpoints/security_headers.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/database_access/transaction_provider.h"
#include "sql_util/table_helpers/admin_column_redactions.h"

namespace Secrets {

    class SecretsHelper;
    using SecretsHelperPtr = std::shared_ptr<SecretsHelper>;

}  // namespace Secrets {

namespace Mail {

    class MailHelper;
    using MailHelperPtr = std::shared_ptr<MailHelper>;
}

namespace Auth {

    class CookieManagerFactory;
    using CookieManagerFactoryPtr = std::shared_ptr<CookieManagerFactory>;

}

// Wraps the Crow application for production and testing purposes.
class WebApp {
public:
    // Phase 4.2 of the security review: CsrfGuard runs LAST in
    // before_handle order so it sees the parsed Cookie header set up
    // by crow::CookieParser. (It actually parses the raw Cookie
    // header itself, but order discipline is cheap insurance.)
    //
    // Phase 7.1 of the security review: SecurityHeaders is the LAST
    // entry — Crow runs after_handle in REVERSE declaration order,
    // so SecurityHeaders.after_handle fires FIRST, ensuring headers
    // land on the response before any other middleware mutates it.
    using AppType = crow::App<
        Endpoints::CloudFrontOriginGuard,
        crow::CookieParser,
        crow::CORSHandler,
        Endpoints::CsrfGuard,
        Endpoints::SecurityHeaders>;

    WebApp(
        DatabaseHelper databaseHelper,
        std::string_view databaseName,
        DbSchema::DatabaseInfo databaseInfo,
        Secrets::SecretsHelperPtr secretsHelper,
        Mail::MailHelperPtr mailHelper,
        TransactionProviderPtr transactionProvider,
        Auth::CookieManagerFactoryPtr cookieManagerFactory,
        TableHelpers::ColumnRedactionSet columnRedactions = {});
    WebApp(const WebApp&) = delete;
    WebApp& operator=(const WebApp&) = delete;
    ~WebApp();

    // Accessors
    AppType& GetApp();
    DatabaseHelper GetDatabaseHelper() const;
    const std::string& GetDatabaseName() const;
    StringArray GetAllowedTables(Transaction& transaction) const;
    bool IsTableAllowed(
        Transaction& transaction, std::string_view tableName) const;
    DbSchema::DatabaseInfo GetDatabaseInfo() const;
    Secrets::SecretsHelperPtr GetSecretsHelper() const;
    Mail::MailHelperPtr GetMailHelper() const;
    TransactionProviderPtr GetTransactionProvider() const;
    Auth::CookieManagerFactoryPtr GetCookieManagerFactory() const;

    // Generic app-service registry (componentization Phase 1.6). The framework
    // WebApp is app-agnostic: app-specific services (e.g. the Square payment
    // client) are registered here by the app at startup via SetService<T> and
    // retrieved by type via GetService<T>. This keeps WebApp — and the framework
    // EndpointAuthHelper that wraps it — free of any app-specific service type.
    // The app-derived AppEndpointAuthHelper turns GetService<T>() into ergonomic
    // typed accessors (e.g. GetSquareClient()). GetService<T> returns nullptr
    // when no service of that type was registered.
    template <typename T>
    void SetService(std::shared_ptr<T> service) {
        services_[std::type_index(typeid(T))] = std::move(service);
    }

    template <typename T>
    std::shared_ptr<T> GetService() const {
        auto it = services_.find(std::type_index(typeid(T)));
        if (it == services_.end()) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(it->second);
    }

    // Phase 3.8: returns the canonical (table, column) redaction set
    // loaded once at startup. Endpoints that construct
    // DatabaseRESTHelper for read paths pass this through so the
    // helper can strip redacted columns at the JSON boundary.
    const TableHelpers::ColumnRedactionSet& GetColumnRedactions() const;

private:
    AppType app_;
    DatabaseHelper databaseHelper_;
    std::string databaseName_;
    DbSchema::DatabaseInfo databaseInfo_;
    Secrets::SecretsHelperPtr secretsHelper_;
    Mail::MailHelperPtr mailHelper_;
    TransactionProviderPtr transactionProvider_;
    Auth::CookieManagerFactoryPtr cookieManagerFactory_;
    TableHelpers::ColumnRedactionSet columnRedactions_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

// Base class for registering an individual route.
// Users should create a class that derives from this class with
// a single instance and override AddRoute() to register their 
// route like:
//
// namespace {
//   class MyRoute : public RoutingBase {
//   public:
//     void AddRoute(WebApp* webApp) override {
//       CROW_ROUTE(webApp->GetApp(), ...
//     }
//   } g_routing;
// }  // namespace
class RoutingBase {
public:
    RoutingBase();
    RoutingBase(const RoutingBase&) = delete;
    RoutingBase& operator=(const RoutingBase&) = delete;
    virtual ~RoutingBase();

    // Put your Crow route here
    virtual void AddRoute(WebApp* webApp) = 0;

    // Call to register all the routes
    static void AddRoutes(WebApp& WebApp);

private:
    static void AddRouting(RoutingBase* routing);
};
