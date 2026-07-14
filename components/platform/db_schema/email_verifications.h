#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kEmailVerificationsTable = "email_verifications";
	inline constexpr std::string_view kEmailVerificationsId = "id";
    inline constexpr std::string_view kEmailVerificationsPersonId = "person_id";
    inline constexpr std::string_view kEmailVerificationsTokenHash = "token_hash";
    inline constexpr std::string_view kEmailVerificationsExpiresAt = "expires_at";
	inline constexpr std::string_view kEmailVerificationsAttempts = "attempts";

	void MakeEmailVerificationsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema