#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminColumnFriendlyNamesTable = "admin_column_friendly_names";
	inline constexpr std::string_view kAdminColumnFriendlyNamesColumnFriendlyNameId = "column_friendly_name_id";
	inline constexpr std::string_view kAdminColumnFriendlyNamesTableName = "table_name";
	inline constexpr std::string_view kAdminColumnFriendlyNamesColumnName = "column_name";
	inline constexpr std::string_view kAdminColumnFriendlyNamesFriendlyName = "friendly_name";

	void MakeAdminColumnFriendlyNamesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema