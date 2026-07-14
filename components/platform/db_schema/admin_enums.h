#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminEnumsTable = "admin_enums";
	inline constexpr std::string_view kAdminEnumsId = "id";
	inline constexpr std::string_view kAdminEnumsName = "name";

	void MakeAdminEnumsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
