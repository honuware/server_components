// Phase 2.3 (SG8): the framework WebApp + RoutingBase implementation. This is
// the app-agnostic web core — it knows nothing about any specific endpoint. It
// ships in honuware_platform. The app endpoint *registration* table (the g_*
// references that force MSVC / GNU ld to retain each endpoint translation unit)
// lives separately in web_app.cpp (knotty_yoga_core), which references app
// endpoints and therefore cannot live in the framework layer.
#include "web_app.h"

#include "util/types.h"
#include "sql_util/table_helpers/allowed_tables.h"

WebApp::WebApp(
    DatabaseHelper databaseHelper,
    std::string_view databaseName,
    DbSchema::DatabaseInfo databaseInfo,
    Secrets::SecretsHelperPtr secretsHelper,
    Mail::MailHelperPtr mailHelper,
    TransactionProviderPtr transactionProvider,
    Auth::CookieManagerFactoryPtr cookieManagerFactory,
    TableHelpers::ColumnRedactionSet columnRedactions /* = {} */)
: databaseHelper_(databaseHelper),
  databaseName_(databaseName),
  databaseInfo_(databaseInfo),
  secretsHelper_(secretsHelper),
  mailHelper_(mailHelper),
  transactionProvider_(transactionProvider),
  cookieManagerFactory_(cookieManagerFactory),
  columnRedactions_(std::move(columnRedactions))
{
}

WebApp::~WebApp() {
}

WebApp::AppType& WebApp::GetApp() {
    return app_;
}

DatabaseHelper WebApp::GetDatabaseHelper() const {
    return databaseHelper_;
}

const std::string& WebApp::GetDatabaseName() const {
    return databaseName_;
}

StringArray WebApp::GetAllowedTables(Transaction& transaction) const {
    TableHelpers::AllowedTables allowedTables(databaseHelper_);
    return allowedTables.GetAllowedTables(transaction);
}

bool WebApp::IsTableAllowed(
    Transaction& transaction, std::string_view tableName) const {
    StringSet allowedTables =
        StringSetFromStringArray(GetAllowedTables(transaction));
    return SetContains(allowedTables, tableName);
}

DbSchema::DatabaseInfo WebApp::GetDatabaseInfo() const {
    return databaseInfo_;
}

Secrets::SecretsHelperPtr WebApp::GetSecretsHelper() const {
    return secretsHelper_;
}

Mail::MailHelperPtr WebApp::GetMailHelper() const {
    return mailHelper_;
}

TransactionProviderPtr WebApp::GetTransactionProvider() const {
    return transactionProvider_;
}

Auth::CookieManagerFactoryPtr WebApp::GetCookieManagerFactory() const {
    return cookieManagerFactory_;
}

const TableHelpers::ColumnRedactionSet& WebApp::GetColumnRedactions() const {
    return columnRedactions_;
}

// Private routing helpers
namespace {

    std::list<RoutingBase*>& RoutingList() {
        static std::list<RoutingBase*> routingList;
        return routingList;
    }

}  // namespace

RoutingBase::RoutingBase() {
    RoutingList().push_back(this);
}

RoutingBase::~RoutingBase() {
}

void RoutingBase::AddRoutes(WebApp& webApp) {
    for (RoutingBase* routing : RoutingList()) {
        routing->AddRoute(&webApp);
    }
    //Used to make sure all the route handlers are in order.
    webApp.GetApp().validate();
}

void RoutingBase::AddRouting(RoutingBase* routing) {
    RoutingList().push_back(routing);
}
