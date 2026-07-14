# Phase 3.1: the config_secrets table helper (TableHelpers::ConfigSecrets) is the
# secrets service's CRUD wrapper for its storage table, so it ships with
# honuware_services (moved here from src/sql_util/table_helpers/). It depends only
# on the data layer (database_access/database_crud_helpers + the config_secrets
# db_schema, also in services).
target_sources(honuware_services
    PRIVATE
    config_secrets.h
    config_secrets.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    config_secrets_test.cpp
)

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})
