#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminColumnEnumsTable = "admin_column_enums";
	inline constexpr std::string_view kAdminColumnEnumsId = "id";
	inline constexpr std::string_view kAdminColumnEnumsAdminEnumId = "admin_enum_id";
	inline constexpr std::string_view kAdminColumnEnumsAdminColumnDataInfoId = "admin_column_data_info_id";

	void MakeAdminColumnEnumsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
