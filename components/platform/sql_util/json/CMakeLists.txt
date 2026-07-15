# Phase 3.1: key_value_table_json (honuware_data) moved to
# components/data/sql_util/json/. database_rest_helper drives the admin_*
# metadata system (it includes sql_util/table_helpers + db_schema admin/photo
# headers, all framework-platform) and stays here in honuware_platform.
target_sources(honuware_platform
    PRIVATE
    database_rest_helper.h
    database_rest_helper.cpp
)

# database_rest_helper_test.cpp exercises this framework helper entirely against
# framework tables (people/orders/items), so it lives here in the component tree
# and provides standalone honuware coverage.
target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    database_rest_helper_test.cpp
)
