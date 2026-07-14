target_sources(honuware_data
    PRIVATE
    get_admin_alerts_in_window.h
    get_admin_alerts_in_window.cpp
    create_stored_procedures.h
    create_stored_procedures.cpp
    now_us.h
    now_us.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    get_admin_alerts_in_window_test.cpp
    now_us_test.cpp
)

target_link_libraries(honuware_data PUBLIC ${BOOST_LIB} ${CURL_LIB} ${CROW_LIB} ${PQXX_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})