#include "delete_item.h"

#include "endpoint_auth_helper.h"
#include "business_logic/images/image_helper.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/database_access/database_metadata.h"
#include "sql_util/table_helpers/photo_support_tables.h"
#include "util/error_response.h"

namespace Endpoints {

    namespace {

        void HandlePost(
            WebApp* webApp,
            const crow::request& req,
            crow::response& resp,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view value) {
            try {
                EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
                endpointAuthHelper.Initialize();
                DeleteItem(endpointAuthHelper, tableName, columnName, value);
                resp = crow::response(200);
            }
            catch (std::exception& e) {
                resp = ErrorResponse::ValidationError(e.what());
            }
            resp.end();
        }

        class SetupRouting : public RoutingBase {
        public:
            void AddRoute(WebApp* webApp) override {
                CROW_ROUTE(
                    webApp->GetApp(),
                    "/api/delete_item/<string>/<string>/<string>")(
                    [=](
                        const crow::request& req,
                        crow::response& resp,
                        const std::string& tableName,
                        const std::string& columnName,
                        const std::string& value) {
                        HandlePost(webApp, req, resp, URLDecode(tableName), URLDecode(columnName), URLDecode(value));
                    });
            }
        } g_setupRouting;

    }  // namespace

    void DeleteItem(
        EndpointAuthHelper& endpointAuthHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view value) {
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            if (!endpointAuthHelper.IsTableAllowed(transaction, tableName)) {
                std::stringstream ss;
                ss << "Table(" << tableName << ") is not an allowed table.";
                throw std::invalid_argument(ss.str());
            }
            // Clean up associated photo if the table supports photos.
            // First check if the photo system is set up by querying
            // information_schema (always succeeds, won't break transaction).
            std::string photoTableExists =
                transaction.RunSqlStatementReturningOneValue(
                    "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
                    "WHERE table_schema = 'public' "
                    "AND table_name = 'photo_support_tables')");
            if (photoTableExists == "t") {
                TableHelpers::PhotoSupportTables photoSupportTables(
                    endpointAuthHelper.GetDatabaseHelper());
                if (photoSupportTables.IsPhotoSupportedForTable(
                        transaction, tableName)) {
                    Images::ImageHelper imageHelper(
                        endpointAuthHelper.GetDatabaseHelper());
                    imageHelper.DeletePhotoForItem(
                        transaction, tableName,
                        std::stoll(std::string(value)));
                }
            }

            DatabaseRESTHelper restHelper(endpointAuthHelper.GetDatabaseHelper());
            restHelper.DeleteTableValue(
                transaction,
                endpointAuthHelper.GetDatabaseInfo(),
                tableName,
                columnName,
                value);
            });
    }

}  // namespace Endpoints