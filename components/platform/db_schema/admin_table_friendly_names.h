#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminTableFriendlyNamesTable = "admin_table_friendly_names";
	inline constexpr std::string_view kAdminTableFriendlyNamesTableFriendlyNameId = "table_friendly_name_id";
	inline constexpr std::string_view kAdminTableFriendlyNamesName = "name";
	inline constexpr std::string_view kAdminTableFriendlyNamesFriendlyName = "friendly_name";
	inline constexpr std::string_view kAdminTableFriendlyNamesDescription = "description";

	void MakeAdminTableFriendlyNamesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema