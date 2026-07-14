# Phase 3.1: sql_util core is the honuware_data component (database_access, json
# key-value bridge, schema, stored_procedures, database_common). table_helpers
# and json/database_rest_helper are honuware_platform and stay under src/.
target_sources(honuware_data
    PRIVATE
    database_common.h
)

add_subdirectory(database_access)
add_subdirectory(json)
add_subdirectory(schema)
add_subdirectory(stored_procedures)

# Data sits directly above foundation, so it links it PUBLIC (inheriting Boost /
# Crow / CURL / date / image include dirs) and adds libpqxx, the one Conan dep
# unique to the data layer.
target_link_libraries(honuware_data PUBLIC honuware_foundation ${PQXX_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})
