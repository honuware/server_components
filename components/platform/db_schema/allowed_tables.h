#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAllowedTablesTable = "allowed_tables";
	inline constexpr std::string_view kAllowedTablesName = "name";

	void MakeAllowedTablesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema