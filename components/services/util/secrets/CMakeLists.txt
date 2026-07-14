# Phase 2.2: util/secrets is part of honuware_services (with util/mail). It links
# honuware_data (SecretsHelper/SecretsAtRest read/write the config_secrets table
# through database_access) and libsodium (secrets_at_rest's at-rest crypto).
# The config_secrets db_schema + table_helper themselves are pulled into
# honuware_services from db_schema/ and sql_util/table_helpers/ — this service
# owns its storage table (see those CMakeLists). The test doubles
# (secrets_helper_test_util) stay in knotty_yoga_tests until honuware_testing
# (Phase 2.6).
target_sources(honuware_services
    PRIVATE
    secret_keys.h
    secret_values.h
    secret_values.cpp
    secrets_at_rest.h
    secrets_at_rest.cpp
    secrets_helper.h
    secrets_helper.cpp
)

# Phase 2.6: the SecretsHelper test double ships in honuware_testing. Phase 4.1:
# it is now app-free — instead of #including business_logic/app_secret_values it
# exposes Secrets::Test::RegisterAppSecretDefaults, and the app test main injects
# App::FillInAppSecretDefaultsString through it. So honuware_testing no longer
# links knotty_yoga_core; the double loads Secrets::Values framework defaults plus
# whatever a composition root registers (none in standalone honuware tests).
target_sources(honuware_testing
    PRIVATE
    secrets_helper_test_util.h
    secrets_helper_test_util.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    secrets_at_rest_test.cpp
    secrets_helper_test.cpp
)

target_link_libraries(honuware_services PUBLIC honuware_data ${LIBSODIUM_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${LIBSODIUM_LIB})