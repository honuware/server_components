# Phase 2.3 (SG6): the image business-logic core (resize/store/serve over the
# framework photo tables) has no app dependencies — it belongs to
# honuware_platform.
target_sources(honuware_platform PRIVATE
    image_info.h
    image_helper.h
    image_helper.cpp
    image_key_value_table.h
    image_key_value_table.cpp
)

target_sources(${HONUWARE_TESTS_TARGET} PUBLIC
    image_helper_test.cpp
    image_key_value_table_test.cpp
)
