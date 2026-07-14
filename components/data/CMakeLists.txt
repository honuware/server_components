# Phase 3.1: honuware_data now lives under components/data/. Its include root is
# THIS directory, so `#include "sql_util/..."` resolves here and data can no
# longer reach up into src/ (db_schema/, table_helpers/, business_logic/, ...) —
# upward includes are a compile-time "file not found". data links foundation, so
# it still sees `util/...` via components/foundation/.
target_include_directories(honuware_data PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(sql_util)
