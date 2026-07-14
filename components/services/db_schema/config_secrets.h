#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kConfigSecretsTable = "config_secrets";
	inline constexpr std::string_view kConfigSecretsId = "id";
	inline constexpr std::string_view kConfigSecretsName = "name";
	inline constexpr std::string_view kConfigSecretsValue = "value";

	void MakeConfigSecretsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema