# Phase 2.2: util/square is honuware_square — the generic Square API client.
# It links honuware_foundation ONLY: square_client.{h,cpp} include just
# util/http/http_client.h and util/json_value.h (both foundation), take the API
# token at construction, and pull in NO secret-key or app headers (audited). CURL
# comes transitively via util/http (foundation). The test double
# (square_client_test_util) stays in knotty_yoga_tests until honuware_testing
# (Phase 2.6).
target_sources(honuware_square
    PRIVATE
    square_errors.h
    square_client.h
    square_client.cpp
)

# Phase 2.6: the Square client test double ships in honuware_testing; its own
# tests + the production client's tests stay in the app test-case bag.
target_sources(honuware_testing
    PRIVATE
    square_client_test_util.h
    square_client_test_util.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    square_client_test.cpp
    square_client_test_util_test.cpp
)

target_link_libraries(honuware_square PUBLIC honuware_foundation)
