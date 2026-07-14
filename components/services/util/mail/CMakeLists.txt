# Phase 2.2: util/mail is part of honuware_services (with util/secrets). It reads
# SMTP config from config_secrets via SecretsHelper (a data-layer read through
# Transaction — conformant, services sit above data) and sends over mailio.
# honuware_services already links honuware_data (via util/secrets); here we add
# mail's own Conan deps. Test double (TestMailHelper) stays in knotty_yoga_tests
# until honuware_testing (Phase 2.6).
target_sources(honuware_services
    PRIVATE
    tenant_branding.h
    tenant_branding.cpp
    mail_helper.h
    mail_helper.cpp
)

# Phase 2.6: the MailHelper test double ships in honuware_testing; its own test
# stays in the app test-case bag.
target_sources(honuware_testing
    PRIVATE
    mail_helper_test_util.h
    mail_helper_test_util.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    mail_helper_test.cpp
    tenant_branding_test.cpp
)

target_link_libraries(honuware_services PUBLIC ${BOOST_LIB} ${OPENSSL_LIB} ${MAILIO_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${BOOST_LIB} ${GTEST_LIB})