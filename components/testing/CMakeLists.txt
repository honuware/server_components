# Phase 3.1.6: honuware_testing — the reusable test harness — moves under
# components/testing/ so it extracts with the other honuware components. This root
# is added to honuware_testing's PUBLIC include dirs; because knotty_yoga_tests
# (and the knottyyoga_tests exe) link honuware_testing PUBLIC, the harness's
# include prefixes are preserved for every consumer:
#   test/src/util/<x>.h              -> components/testing/test/src/util/<x>.h
#   endpoints/endpoint_test_helper.h -> components/testing/endpoints/endpoint_test_helper.h
#
# Only test/src/util/* + endpoint_test_helper move here. The per-component service
# test doubles (util/mail, util/secrets, util/square, util/http, business_logic/
# auth) stay CO-LOCATED with the production code they double — they are compiled
# into honuware_testing from their own component dirs, which is the more natural
# home and keeps a double next to the thing it fakes.
target_include_directories(honuware_testing PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(test/src/util)
add_subdirectory(endpoints)
