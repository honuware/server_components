# Phase 3.1: util core is the honuware_foundation component (no includes outside
# util/), now physically under components/foundation/util/. mail/secrets are
# honuware_services and square is honuware_square — both moved to their own
# component roots.
target_sources(honuware_foundation
    PRIVATE
    bounding_rect.h
    bounding_rect.cpp
    date_time_util.h
    date_time_util.cpp
    destructive_guard.h
    destructive_guard.cpp
    env.h
    env.cpp
    error_codes.h
    error_response.h
    error_response.cpp
    ical_generator.h
    ical_generator.cpp
    file_util.h
    file_util.cc
    json_util.h
    json_util.cpp
    json_value.h
    image_resize.h
    image_resize.cpp
    logging.h
    logging.cpp
    types.h
    types.cpp
    thread_pool.h
    thread_pool.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    bounding_rect_test.cpp
    date_time_util_test.cpp
    destructive_guard_test.cpp
    env_test.cpp
    error_response_test.cpp
    ical_generator_test.cpp
    image_resize_test.cpp
    json_util_test.cpp
    json_value_test.cpp
    logging_test.cpp
    types_test.cpp
    thread_pool_test.cpp
)

# Phase 3.1: mail/, secrets/ (honuware_services) and square/ (honuware_square)
# moved out to components/services/ and components/square/. Only http (foundation)
# remains under this root.
add_subdirectory(http)

# Foundation owns the low-level Conan libs its public headers expose: Boost and
# Crow (json_value.h wraps crow::json), date, and the image codecs used by
# image_resize (PNG/TIFF/ZLIB; jpeg is pulled in transitively by TIFF — there is
# no standalone JPEG_LIB in ConanLibImports). CURL is added by util/http.
target_link_libraries(honuware_foundation PUBLIC ${BOOST_LIB} ${CROW_LIB} ${DATE_LIB} ${PNG_LIB} ${TIFF_LIB} ${ZLIB_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC ${BOOST_LIB} ${DATE_LIB} ${PNG_LIB} ${TIFF_LIB} ${GTEST_LIB})