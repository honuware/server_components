# Phase 2.6: the reusable database test harness + matchers + json/table test
# utils are honuware_testing. They are app-agnostic — global_database_test_support
# and database_test_helper take the composed schema as input (the app-side test
# main injects MakeDatabaseInfo) rather than calling the app composition root.
target_sources(honuware_testing
    PUBLIC
        database_test_helper.h
        database_test_helper.cpp
        database_helper_test.h
        global_database_test_support.h
        global_database_test_support.cpp
        json_test_util.h
        json_test_util.cpp
        json_value_matcher.h
        json_value_matcher.cpp
        table_matcher.h
        table_matcher.cpp
        test_helper.h
        test_helper.cpp
        test_transaction_provider.h
        test_transaction_provider.cpp
)

# The harness's own unit tests + the app-domain payment/scheduling table shims
# stay in the app test-case bag (${HONUWARE_TESTS_TARGET}).
target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
        file_util_test.cc
        global_database_test_support_test.cpp
        json_test_util_test.cpp
        json_value_matcher_test.cpp
        table_matcher_test.cpp
        test_helper_test.cpp
        payment_table_test_helper.h
        payment_table_test_helper.cpp
)

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${CROW_LIB} ${GTEST_LIB})
