# Tenant Theming and Branding Phase 1 (decision D11 — framework-first): the
# content-slot registry a tenant's copy arrives through, and the strict
# server-side validation (D10) shared by the site_info read path and the Phase 6
# Site Theme write path. Brand-free: the app supplies default VALUES only.
target_sources(honuware_platform PRIVATE
    site_value_validation.h
    site_value_validation.cpp
    site_content_slots.h
    site_content_slots.cpp
    site_info_app_blocks.h
    site_info_app_blocks.cpp
    site_theme_tokens.h
    site_theme_tokens.cpp
    site_font_inventory.h
    site_font_inventory.cpp
    # Phase 9 — a whole look as a portable artifact (theme bundles).
    theme_bundle.h
    theme_bundle_assets.h
    theme_bundle_assets.cpp
    theme_bundle_sections.h
    theme_bundle_sections.cpp
    theme_bundle_migrations.h
    theme_bundle_migrations.cpp
    theme_bundle_json.h
    theme_bundle_json.cpp
    theme_bundle_export.h
    theme_bundle_export.cpp
    theme_bundle_import.h
    theme_bundle_import.cpp
    theme_bundle_zip.h
    theme_bundle_zip.cpp
    theme_bundle_directory.h
    theme_bundle_directory.cpp
)

# The .zip transport for theme bundles (Phase 9 / OQ-TF1). Deliberately a
# library rather than a hand-rolled reader: the parsing side takes untrusted
# input from a file picker.
target_link_libraries(honuware_platform PUBLIC ${LIBZIP_LIB})

target_sources(${HONUWARE_TESTS_TARGET} PUBLIC
    site_value_validation_test.cpp
    site_content_slots_test.cpp
    site_info_app_blocks_test.cpp
    site_theme_tokens_test.cpp
    site_font_inventory_test.cpp
    theme_bundle_assets_test.cpp
    theme_bundle_sections_test.cpp
    theme_bundle_migrations_test.cpp
    theme_bundle_json_test.cpp
    theme_bundle_round_trip_test.cpp
    theme_bundle_zip_test.cpp
    theme_bundle_directory_test.cpp
)
