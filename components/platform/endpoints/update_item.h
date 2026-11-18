#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    // Expected data like:
    // {
    //   table_name: {table_name},
    //   column_name: {column_name},
    //   column_value: {column_value},
    //   value: {object_data}
    // }
    //
    // A write needs write access to the table (EndpointAuthHelper::
    // RequireTableWriteAccess: an admin, or a grant through
    // admin_table_permissions), not just read access. Returns false when that
    // is refused; the 401/403 is then already in endpointAuthHelper.Response().
    bool UpdateItem(
        EndpointAuthHelper& endpointAuthHelper,
		const Json::Value& message);

}  // namespace Endpoints