# Phase 3.1: key_value_table_json is the pure KeyValueTable<->JSON bridge — it
# belongs to honuware_data and moved here with the rest of the data layer.
# database_rest_helper (the admin-metadata driver) is honuware_platform and stays
# under src/sql_util/json/.
target_sources(honuware_data
    PRIVATE
    key_value_table_json.h
    key_value_table_json.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    key_value_table_json_test.cpp
)

target_link_libraries(honuware_data PUBLIC ${BOOST_LIB} ${CURL_LIB} ${CROW_LIB} ${PQXX_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})
