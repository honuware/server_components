# Phase 3.1: config_secrets is the secrets service's own storage schema, so it
# ships with honuware_services (moved here from src/db_schema/). It depends only
# on the data layer (sql_util/schema database_info); its non-owner users
# (make_framework_tables, admin_secrets, login_gate) sit above services and
# consume it downward via components/services/'s PUBLIC include dir.
target_sources(honuware_services
    PRIVATE
    config_secrets.h
    config_secrets.cpp
)
