# Phase 3.1.2: honuware_platform begins moving under components/platform/. This
# root is ADDED to platform's PUBLIC include dirs while src/ is KEPT (platform
# still has framework files — framework db_schema/table_helpers/endpoints — in
# src/ until steps 3.1.3–3.1.4 move them and 3.1.5 flips the root). So platform
# resolves moved framework headers here and not-yet-moved ones via src/; app
# code (knotty_yoga_core) reaches everything here by linking platform.
#
# First pieces: the framework business-logic subdirs auth/images/migration (clean
# whole-directory moves; their payment/scheduling/skills siblings are app and
# stay in src/business_logic/).
target_include_directories(honuware_platform PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(business_logic/auth)
add_subdirectory(business_logic/images)
add_subdirectory(business_logic/migration)

# Phase 3.1.3: the framework schema + table-helpers + admin-metadata driver.
#   db_schema/               — framework table DDL + column constants (people,
#                              permissions, roles, sessions, device_tokens,
#                              admin_* metadata, photos, ...); app tables stay in
#                              src/db_schema/.
#   sql_util/table_helpers/  — framework CRUD wrappers (admin_*, photo_*,
#                              auth_events, ...); app table helpers stay in
#                              src/sql_util/table_helpers/.
#   sql_util/json/           — database_rest_helper, the admin_* metadata REST
#                              driver (whole dir; key_value_table_json already
#                              went to components/data in 3.1).
add_subdirectory(db_schema)
add_subdirectory(sql_util/table_helpers)
add_subdirectory(sql_util/json)

# Phase 3.1.4: the framework endpoints + web core — the app-agnostic WebApp /
# RoutingBase impl (web_app_framework), the framework request/session façade
# (endpoint_auth_helper), the middleware guards (cloudfront origin, CSRF, security
# headers), health, and the generic CRUD + schema-metadata endpoints (add_item,
# update_item, delete_item, get_row(s)/by_values, get_table_rows, get_fk_options,
# resolve_fk_display, db_schema). App endpoints + web_app.cpp registration table +
# endpoint_test_helper stay in src/endpoints/.
add_subdirectory(endpoints)
