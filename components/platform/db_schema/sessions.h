#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kSessionsTable = "sessions";
	inline constexpr std::string_view kSessionsId = "id";
    inline constexpr std::string_view kSessionsUuid = "uuid";
    inline constexpr std::string_view kSessionsPersonId = "person_id";
    inline constexpr std::string_view kSessionsCreatedAt = "created_at";
	inline constexpr std::string_view kSessionsLastSeenAt = "last_seen_at";
    inline constexpr std::string_view kSessionsExpiresAt = "expires_at";
    inline constexpr std::string_view kSessionsRevoked = "revoked";

	void MakeSessionsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema