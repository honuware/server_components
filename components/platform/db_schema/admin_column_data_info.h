#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kAdminColumnDataInfoTable = "admin_column_data_info";
	inline constexpr std::string_view kAdminColumnDataInfoColumnDataInfoId = "column_data_info_id";
	inline constexpr std::string_view kAdminColumnDataInfoTableName = "table_name";
	inline constexpr std::string_view kAdminColumnDataInfoColumnName = "column_name";
	inline constexpr std::string_view kAdminColumnDataInfoLabel = "label";
	inline constexpr std::string_view kAdminColumnDataInfoHint = "hint";
	inline constexpr std::string_view kAdminColumnDataInfoPlaceHolder = "place_holder";
	inline constexpr std::string_view kAdminColumnDataInfoRegex = "regex";
	inline constexpr std::string_view kAdminColumnDataInfoHtmlInputType = "html_input_type";
	inline constexpr std::string_view kAdminColumnDataInfoRequired = "required";
	inline constexpr std::string_view kAdminColumnDataInfoMaxLength = "max_length";
	inline constexpr std::string_view kAdminColumnDataInfoDefaultValue = "default_value";
	inline constexpr std::string_view kAdminColumnDataInfoRows = "rows";
	inline constexpr std::string_view kAdminColumnDataInfoHidden = "hidden";
	inline constexpr std::string_view kAdminColumnDataInfoReadonly = "readonly";

	void MakeAdminColumnDataInfoTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema