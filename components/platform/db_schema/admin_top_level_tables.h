#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminTopLevelTablesTable = "admin_top_level_tables";
	inline constexpr std::string_view kAdminTopLevelTablesName = "name";

	void MakeAdminTopLevelTablesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema