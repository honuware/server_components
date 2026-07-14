#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminTableDisplayTemplateTable = "admin_table_display_template";
	inline constexpr std::string_view kAdminTableDisplayTemplateName = "name";
	inline constexpr std::string_view kAdminTableDisplayTemplateTemplate = "display_template";

	void MakeAdminTableDisplayTemplateTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
