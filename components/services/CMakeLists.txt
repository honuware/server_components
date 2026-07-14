# Phase 3.1: honuware_services (util/secrets + util/mail) now lives under
# components/services/. Its include root is THIS directory. The secrets service
# owns its storage table, so the config_secrets db_schema + table_helper move
# here too (under components/services/db_schema/ and
# components/services/sql_util/table_helpers/) — that keeps
# `#include "db_schema/config_secrets.h"` / `"sql_util/table_helpers/config_secrets.h"`
# resolving inside services' own root. services links data, so it still sees
# `sql_util/database_access/...` (Transaction) via components/data/.
target_include_directories(honuware_services PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(util/secrets)
add_subdirectory(util/mail)
add_subdirectory(db_schema)
add_subdirectory(sql_util/table_helpers)
