# Phase 3.1: key_value_table_json (honuware_data) moved to
# components/data/sql_util/json/. database_rest_helper drives the admin_*
# metadata system (it includes sql_util/table_helpers + db_schema admin/photo
# headers, all framework-platform) and stays here in honuware_platform.
target_sources(honuware_platform
    PRIVATE
    database_rest_helper.h
    database_rest_helper.cpp
)

# database_rest_helper_test.cpp exercises this framework helper against an APP
# table (price_schedules) as its fixture, so it is app-coupled and was relocated
# to src/sql_util/ (Phase 4.1). The production helper stays here in
# honuware_platform. Follow-up: port that test to a framework table so it can
# return to the component tree and restore standalone honuware coverage.
