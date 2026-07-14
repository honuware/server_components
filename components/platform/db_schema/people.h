#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kPeopleTable = "people";
	inline constexpr std::string_view kPeopleId = "id";
	inline constexpr std::string_view kPeopleEmail = "email";
	inline constexpr std::string_view kPeopleFirstName = "first_name";
	inline constexpr std::string_view kPeopleLastName = "last_name";
	inline constexpr std::string_view kPeoplePasswordHash = "password_hash";
    inline constexpr std::string_view kPeopleEmailVerifiedAt = "email_verified_at";
    inline constexpr std::string_view kPeopleMustChangePassword = "must_change_password";
    inline constexpr std::string_view kPeopleCreatedAt = "created_at";
    inline constexpr std::string_view kPeopleUpdatedAt = "updated_at";
    // Lockout state. Written only by the auth path (Phase 5 wires this up);
    // these are auth-internal and must NOT be exposed via the generic CRUD
    // endpoints. Phase 1.7 of the security review.
    inline constexpr std::string_view kPeopleFailedLoginAttempts = "failed_login_attempts";
    inline constexpr std::string_view kPeopleLockedUntil = "locked_until";

	void MakePeopleTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema