# Tenant Theming and Branding Phase 1 (decision D11 — framework-first): the
# content-slot registry a tenant's copy arrives through, and the strict
# server-side validation (D10) shared by the site_info read path and the Phase 6
# Site Theme write path. Brand-free: the app supplies default VALUES only.
target_sources(honuware_platform PRIVATE
    site_value_validation.h
    site_value_validation.cpp
    site_content_slots.h
    site_content_slots.cpp
    site_theme_tokens.h
    site_theme_tokens.cpp
    site_font_inventory.h
    site_font_inventory.cpp
)

target_sources(${HONUWARE_TESTS_TARGET} PUBLIC
    site_value_validation_test.cpp
    site_content_slots_test.cpp
    site_theme_tokens_test.cpp
    site_font_inventory_test.cpp
)
