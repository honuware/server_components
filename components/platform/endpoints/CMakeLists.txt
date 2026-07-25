# Phase 3.1.4: the framework endpoints + web core (honuware_platform),
# split from src/endpoints/. The app endpoints + web_app.cpp registration
# table + endpoint_test_helper stay in src/endpoints/. CROW reaches these
# framework endpoints transitively (platform -> data -> foundation, both of
# which link CROW_LIB PUBLIC); only the tests need an explicit GTEST link.

target_sources(honuware_platform
    PRIVATE
    web_app_types.h
    web_app.h
    web_app_framework.cpp
    endpoint_auth_helper.h
    endpoint_auth_helper.cpp
    cloudfront_origin_guard.h
    cloudfront_origin_guard.cpp
    tenant_resolution_guard.h
    tenant_resolution_guard.cpp
    csrf_guard.h
    csrf_guard.cpp
    security_headers.h
    security_headers.cpp
    health.h
    health.cpp
    site_info.h
    site_info.cpp
    add_item.h
    add_item.cpp
    add_item_fetch_primary_key.h
    add_item_fetch_primary_key.cpp
    update_item.h
    update_item.cpp
    delete_item.h
    delete_item.cpp
    get_row.h
    get_row.cpp
    get_rows_by_column.h
    get_rows_by_column.cpp
    get_row_by_values.h
    get_row_by_values.cpp
    get_table_rows.h
    get_table_rows.cpp
    get_filtered_table_rows.h
    get_filtered_table_rows.cpp
    get_fk_options.h
    get_fk_options.cpp
    resolve_fk_display.h
    resolve_fk_display.cpp
    db_schema.h
    db_schema.cpp
    # Phase 0.2a (H8): account/user + photo endpoints extracted from the app.
    account_activation.h
    account_activation.cpp
    verify.h
    verify.cpp
    login.h
    login.cpp
    logout.h
    logout.cpp
    me.h
    me.cpp
    remember.h
    remember.cpp
    get_user_info.h
    get_user_info.cpp
    set_user_info.h
    set_user_info.cpp
    update_user_password.h
    update_user_password.cpp
    has_photo.h
    has_photo.cpp
    upload_photo.h
    upload_photo.cpp
    upload_user_photo.h
    upload_user_photo.cpp
    delete_photo.h
    delete_photo.cpp
    get_photo.h
    get_photo.cpp
    # Phase 0.2b (H8): register + get_scaled_photo, with their framework seams
    # (PostRegisterHook / PublicPhotoTables — app-configured via WebApp::SetService).
    registration_hooks.h
    register.h
    register.cpp
    public_photo_tables.h
    get_scaled_photo.h
    get_scaled_photo.cpp
    register_framework_endpoints.h
    register_framework_endpoints.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    add_item_test.cpp
    add_item_fetch_primary_key_test.cpp
    db_schema_test.cpp
    web_app_types_test.cpp
    delete_item_test.cpp
    get_filtered_table_rows_test.cpp
    get_fk_options_test.cpp
    resolve_fk_display_test.cpp
    get_rows_by_column_test.cpp
    get_row_test.cpp
    get_row_by_values_test.cpp
    update_item_test.cpp
    endpoint_auth_helper_test.cpp
    cloudfront_origin_guard_test.cpp
    tenant_resolution_guard_test.cpp
    csrf_guard_test.cpp
    security_headers_test.cpp
    health_test.cpp
    site_info_test.cpp
    web_app_tenancy_test.cpp
    endpoint_auth_helper_tenancy_test.cpp
    get_table_rows_test.cpp
    # Phase 0.2a (H8): account/user + photo endpoint tests.
    account_activation_test.cpp
    verify_test.cpp
    login_test.cpp
    logout_test.cpp
    me_test.cpp
    remember_test.cpp
    get_user_info_test.cpp
    set_user_info_test.cpp
    update_user_password_test.cpp
    has_photo_test.cpp
    upload_photo_test.cpp
    upload_user_photo_test.cpp
    delete_photo_test.cpp
    get_photo_test.cpp
)

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${GTEST_LIB})
