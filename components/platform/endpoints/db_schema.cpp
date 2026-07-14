#include "db_schema.h"

#include "endpoint_auth_helper.h"
#include "util/error_response.h"
#include "util/secrets/secret_keys.h"
#include "sql_util/json/database_rest_helper.h"
#include "sql_util/database_access/database_metadata.h"
#include "sql_util/table_helpers/admin_table_friendly_names.h"
#include "sql_util/table_helpers/admin_column_friendly_names.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_enum_values.h"
#include "sql_util/table_helpers/admin_column_enums.h"
#include "sql_util/table_helpers/admin_nested_tables.h"
#include "sql_util/table_helpers/admin_top_level_tables.h"
#include "sql_util/table_helpers/admin_table_display_template.h"


namespace Endpoints {

    namespace {

        void HandlePost(WebApp* webApp, const crow::request& req, crow::response& resp) {
            try {
                EndpointAuthHelper endpointAuthHelper(*webApp, req, resp);
                endpointAuthHelper.Initialize();
                Json::Value result = GetDbSchema(endpointAuthHelper);
                resp.code = 200;
                resp.set_header("Content-Type", "application/json");
				resp.write(result.ToString());
            }
            catch (std::invalid_argument& e) {
                // Phase 7.3 of the security review: input-validation
                // failures from our own helpers come through as
                // std::invalid_argument — 400 with the helper's
                // text (not user-supplied; safe to echo).
                resp = ErrorResponse::ValidationError(e.what());
            }
            catch (std::exception& e) {
                // Phase 7.3 of the security review: anything else
                // (DB errors, unexpected failures) is internal —
                // log the context server-side, return generic 500.
                resp = ErrorResponse::InternalError(e.what());
            }
            resp.end();
        }

        class SetupRouting : public RoutingBase {
        public:
            void AddRoute(WebApp* webApp) override {
                CROW_ROUTE(webApp->GetApp(), "/api/get_db_schema")(
                    [=](const crow::request& req, crow::response& resp) {
                        HandlePost(webApp, req, resp);
                    });
            }
        } g_setupRouting;

    }  // namespace

    Json::Value GetDbSchema(EndpointAuthHelper& endpointAuthHelper) {
        Json::Value result;
        endpointAuthHelper.GetTransactionProvider()->RunInTransaction([&](Transaction& transaction) {
            DatabaseRESTHelper restHelper(endpointAuthHelper.GetDatabaseHelper());

            TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(
                endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(
                endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::AdminColumnDataInfo adminColumnDataInfo(
                endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::AdminEnumValues adminEnumValues(
                endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::AdminColumnEnums adminColumnEnums(
                endpointAuthHelper.GetDatabaseHelper());
            TableHelpers::AdminTopLevelTables adminTopLevelTables(
                endpointAuthHelper.GetDatabaseHelper());
            StringArray topLevelTables =
                adminTopLevelTables.GetAdminTopLevelTables(transaction);
            TableHelpers::AdminNestedTables adminNestedTables(
                endpointAuthHelper.GetDatabaseHelper());
            StringArray nestedTables =
                adminNestedTables.GetAdminNestedTables(transaction);

            // Fetch display templates
            TableHelpers::AdminTableDisplayTemplate displayTemplateHelper(
                endpointAuthHelper.GetDatabaseHelper());
            KeyValueTable displayTemplates =
                displayTemplateHelper.GetAllDisplayTemplates(transaction);

            // Fetch FK picker preload threshold from config
            int fkPickerPreloadThreshold = 50;
            auto secrets = endpointAuthHelper.GetSecretsHelper();
            std::string thresholdStr = secrets->LookupSecret(
                transaction, Secrets::kFkPickerPreloadThreshold);
            if (!thresholdStr.empty()) {
                fkPickerPreloadThreshold = std::stoi(thresholdStr);
            }

            result = restHelper.DatabaseMetadata(
                transaction,
                endpointAuthHelper.GetDatabaseInfo(),
                endpointAuthHelper.GetAllowedTables(transaction),
                topLevelTables,
                nestedTables,
                adminTableFriendlyNames,
                adminColumnFriendlyNames,
                adminColumnDataInfo,
                adminEnumValues,
                adminColumnEnums,
                displayTemplates,
                fkPickerPreloadThreshold);
            });
        return result;
    }

}  // namespace Endpoints