# Phase 2.1: util/http is part of honuware_foundation (the "http/" entry in the
# foundation file list). CURL is the transport it wraps.
target_sources(honuware_foundation
    PRIVATE
    http_client.h
    http_client.cpp
)

# Phase 2.6: the HttpClient test double ships in honuware_testing; its own test
# stays in the app test-case bag.
target_sources(honuware_testing
    PRIVATE
    http_client_test_util.h
    http_client_test_util.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    http_client_test_util_test.cpp
)

target_link_libraries(honuware_foundation PUBLIC ${CURL_LIB})
