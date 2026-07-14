#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminEnumValuesTable = "admin_enum_values";
	inline constexpr std::string_view kAdminEnumValuesId = "id";
	inline constexpr std::string_view kAdminEnumValuesAdminEnumId = "admin_enum_id";
	inline constexpr std::string_view kAdminEnumValuesName = "name";
	inline constexpr std::string_view kAdminEnumValuesValue = "value";

	void MakeAdminEnumValuesTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
