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

# Phase 13: http_client_test.cpp is the ONE test that exercises the real
# libcurl-backed MakeHttpClient() against a live Crow server on 127.0.0.1. Crow
# arrives transitively via honuware_foundation's PUBLIC ${CROW_LIB}, so this adds
# no new dependency.
target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    http_client_test_util_test.cpp
    http_client_test.cpp
)

target_link_libraries(honuware_foundation PUBLIC ${CURL_LIB})
