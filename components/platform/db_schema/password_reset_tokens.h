#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Finishing website Phase 11.1: the pending "forgot password" link, at most
	// one per person. Same shape as email_verifications, but a table of its own
	// so a pending verification and a pending reset can exist side by side.
	//
	// The token hash is a credential. Like email_verifications, this table is
	// schema only: it is NEVER registered in admin_top_level_tables or
	// admin_nested_tables, so no generic CRUD endpoint can read a row.
	inline constexpr std::string_view kPasswordResetTokensTable = "password_reset_tokens";
	inline constexpr std::string_view kPasswordResetTokensId = "id";
	inline constexpr std::string_view kPasswordResetTokensPersonId = "person_id";
	// Hex of the generic hash of the secret the reset mail carries. The
	// secret itself is never stored.
	inline constexpr std::string_view kPasswordResetTokensTokenHash = "token_hash";
	// Microseconds since epoch, like every other *_at column.
	inline constexpr std::string_view kPasswordResetTokensExpiresAt = "expires_at";
	// Wrong secrets presented against this link so far.
	inline constexpr std::string_view kPasswordResetTokensAttempts = "attempts";
	inline constexpr std::string_view kPasswordResetTokensCreatedAt = "created_at";

	void MakePasswordResetTokensTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
