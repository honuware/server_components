# Phase 3.1: honuware_square (the generic Square API client) now lives under
# components/square/. Its include root is THIS directory, so
# `#include "util/square/square_client.h"` resolves here. square links foundation
# ONLY, so it sees `util/http/...` / `util/json_value.h` via components/foundation/
# and nothing above foundation — an upward include fails to compile.
target_include_directories(honuware_square PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(util/square)
