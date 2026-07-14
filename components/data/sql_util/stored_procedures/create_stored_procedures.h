#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace StoredProcedures {

void CreateStoredProceduresBeforeTables(Transaction& transaction);
void CreateStoredProceduresAfterTables(Transaction& transaction);

std::string GenerateStoredProceduresBeforeTablesSql();
std::string GenerateStoredProceduresAfterTablesSql();

} // namespace StoredProcedures

