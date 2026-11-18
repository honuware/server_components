#pragma once

#include <crow.h>

#include "util/json_value.h"

#include "endpoint_auth_helper.h"

namespace Endpoints {

    // POST /api/delete_item/{tableName}/{columnName}/{value}
    //
    // POST only, so the CSRF guard covers it: a GET would let a link on another
    // site delete rows as whoever clicks it. A delete needs write access to the
    // table (EndpointAuthHelper::RequireTableWriteAccess: an admin, or a grant
    // through admin_table_permissions), not just read access. Returns false when
    // that is refused; the 401/403 is then already in
    // endpointAuthHelper.Response().
    bool DeleteItem(
        EndpointAuthHelper& endpointAuthHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view value);

}  // namespace Endpoints