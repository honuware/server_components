# Phase 2.3 (SG7): the auth business-logic core (sessions, cookies, login gate,
# person model, service account, token cleanup) is app-free — audited to have no
# includes of business_logic/payment, /scheduling, or /skills. Its only endpoint
# edges (server_config / cookie_manager -> endpoints/web_app*) point at web-core
# FRAMEWORK, which also lands in honuware_platform. So the whole directory moves
# into platform.
target_sources(honuware_platform
    PRIVATE
    auth_cookies.h
    auth_cookies.cpp
    auth_helper.h
    auth_helper.cpp
    login_gate.h
    login_gate.cpp
    person.h
    person.cpp
    person_verify_mail.h
    person_verify_mail.cpp
    proxy_trust.h
    proxy_trust.cpp
    quick_account_helper.h
    quick_account_helper.cpp
    quick_account_welcome_mail.h
    quick_account_welcome_mail.cpp
    server_config.h
    server_config.cpp
    cookie_manager.h
    cookie_manager.cpp
    session.h
    session.cpp
    service_account.h
    service_account.cpp
    token_cleanup_helper.h
    token_cleanup_helper.cpp
)

# Phase 2.6: the CookieManager test double ships in honuware_testing — it is a
# framework auth double that endpoint_test_helper wires into the test WebApp.
# Phase 4.2: person_verify_mail_test_util joins it in honuware_testing. It is a
# reusable FRAMEWORK auth test utility (it wraps the framework person_verify_mail
# builder to recover the encoded token from a sent verification-mail body),
# consumed by BOTH framework tests (person_test) AND app endpoint tests
# (register / verify / account_activation). Every OTHER shared test util already
# lives in honuware_testing; leaving this one in the test-CASE bag
# (${HONUWARE_TESTS_TARGET}) worked only while the framework and app test cases
# shared one library — once extraction split them into honuware_tests vs the app's
# test lib, the app tests could no longer resolve it. The harness is the one layer
# both bags link, so it belongs here (next to cookie_manager_test_util).
target_sources(honuware_testing
    PRIVATE
    cookie_manager_test_util.h
    cookie_manager_test_util.cpp
    person_verify_mail_test_util.h
    person_verify_mail_test_util.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    auth_cookies_test.cpp
    auth_helper_test.cpp
    login_gate_test.cpp
    person_test.cpp
    person_verify_mail_test.cpp
    proxy_trust_test.cpp
    quick_account_helper_test.cpp
    quick_account_welcome_mail_test.cpp
    server_config_test.cpp
    cookie_manager_test.cpp
    service_account_test.cpp
    session_test.cpp
    token_cleanup_helper_test.cpp
)

# auth (now in honuware_platform) hashes passwords with libsodium. Platform also
# gets libsodium transitively via honuware_services (util/secrets), but link it
# explicitly here since the auth objects that reference it now live in platform.
target_link_libraries(honuware_platform PUBLIC ${LIBSODIUM_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${LIBSODIUM_LIB})