# Phase 3.1: honuware_foundation now lives under components/foundation/. Its
# include root is THIS directory, so `#include "util/..."` resolves to
# components/foundation/util/... . Because foundation no longer has src/ on its
# include path, an upward include (sql_util/, db_schema/, business_logic/,
# endpoints/, ...) becomes a compile-time "file not found" — the layer boundary
# is now a hard wall, not a convention. Consumers reach foundation's headers via
# this PUBLIC include dir, propagated by target_link_libraries(honuware_foundation).
target_include_directories(honuware_foundation PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(util)
