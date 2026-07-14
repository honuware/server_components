#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace StoredProcedures {

void CreateNowUs(Transaction& transaction);
std::string GenerateNowUsSql();

} // namespace StoredProcedures

