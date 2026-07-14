target_sources(honuware_data
    PRIVATE
    column_info.h
    column_info.cpp
    database_info.h
    database_info.cpp
    foreign_key_manager.h
    foreign_key_manager.cpp
    table_info.h
    table_info.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    column_info_test.cpp
    database_info_test.cpp
    foreign_key_manager_test.cpp
    table_info_test.cpp
)

target_link_libraries(honuware_data PUBLIC ${BOOST_LIB} ${CURL_LIB} ${CROW_LIB} ${PQXX_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})