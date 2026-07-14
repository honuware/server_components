#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminNestedTablesTable = "admin_nested_tables";
	inline constexpr std::string_view kAdminNestedTablesName = "name";

	void MakeAdminNestedTablesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
