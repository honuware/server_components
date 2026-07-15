#include "endpoints/register_framework_endpoints.h"

// Take the address of every framework endpoint function below so MSVC / the GNU
// linker cannot dead-strip its translation unit (and the self-registering
// RoutingBase it carries) from the honuware_platform static library. This mirrors
// the app's web_app.cpp registration table, but for the framework generic-CRUD,
// admin-metadata, and health endpoints only.
#include "endpoints/add_item.h"
#include "endpoints/add_item_fetch_primary_key.h"
#include "endpoints/db_schema.h"
#include "endpoints/delete_item.h"
#include "endpoints/get_filtered_table_rows.h"
#include "endpoints/get_fk_options.h"
#include "endpoints/get_row.h"
#include "endpoints/get_row_by_values.h"
#include "endpoints/get_rows_by_column.h"
#include "endpoints/get_table_rows.h"
#include "endpoints/health.h"
#include "endpoints/resolve_fk_display.h"
#include "endpoints/update_item.h"

namespace {

    auto g_AddItem = &Endpoints::AddItem;
    auto g_AddItemFetchPrimaryKey = &Endpoints::AddItemFetchPrimaryKey;
    auto g_GetDbSchema = &Endpoints::GetDbSchema;
    auto g_DeleteItem = &Endpoints::DeleteItem;
    auto g_GetFilteredTableRows = &Endpoints::GetFilteredTableRows;
    auto g_GetFkOptions = &Endpoints::GetFkOptions;
    auto g_ResolveFkDisplay = &Endpoints::ResolveFkDisplay;
    auto g_GetRowsByColumn = &Endpoints::GetRowsByColumn;
    auto g_GetRow = &Endpoints::GetRow;
    auto g_GetRowByValues = &Endpoints::GetRowByValues;
    auto g_GetTableRows = &Endpoints::GetTableRows;
    auto g_GetHealth = &Endpoints::GetHealth;
    auto g_UpdateItem = &Endpoints::UpdateItem;

}  // namespace

namespace Endpoints {

void RegisterFrameworkEndpoints() {}

}  // namespace Endpoints
