#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

inline constexpr std::string_view kAdminAlertsTable = "admin_alerts";
inline constexpr std::string_view kAdminAlertsId = "id";
inline constexpr std::string_view kAdminAlertsCreatedAt = "created_at";
inline constexpr std::string_view kAdminAlertsDescription = "description";

void MakeAdminAlertsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema