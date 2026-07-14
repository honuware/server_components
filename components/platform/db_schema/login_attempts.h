#pragma once

#include "sql_util/database_access/transaction.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/database_common.h"

namespace DbSchema {

	// Phase 5.1 of the security review: tracks every authentication
	// attempt — successful and failed — for the rate-limit gate to
	// consult. Holds short-lived data (default 30-day retention via
	// the periodic cleanup job in Phase 5.8).
	//
	// Privacy note: this table holds PII (email + IP address) and
	// MUST NOT be exposed via the generic CRUD endpoints. It is
	// intentionally NOT registered in admin_top_level_tables. Any
	// admin-facing reporting must go through a dedicated endpoint
	// that masks fields appropriately.
	inline constexpr std::string_view kLoginAttemptsTable = "login_attempts";
	inline constexpr std::string_view kLoginAttemptsId = "id";
	// Stored as CITEXT to match people.email — case-insensitive
	// comparisons without per-call lowercasing.
	inline constexpr std::string_view kLoginAttemptsEmailLower = "email_lower";
	// Stored as TEXT (not Postgres `inet`) so the schema-helper layer
	// stays type-agnostic. We never need inet operations on these
	// values; they're equality-matched and counted, that's all.
	inline constexpr std::string_view kLoginAttemptsIp = "ip";
	// Microseconds since epoch (matches the convention used by
	// people.created_at, sessions.created_at, etc.).
	inline constexpr std::string_view kLoginAttemptsAttemptedAt = "attempted_at";
	inline constexpr std::string_view kLoginAttemptsSuccess = "success";
	// One of "login", "verify", "remember" — see Phase 5.6.
	inline constexpr std::string_view kLoginAttemptsKind = "kind";

	// Kind values used by the gate / recorder. Keeping these as
	// constants prevents typo drift between the table helper and
	// callers in login.cpp / verify.cpp / remember.cpp.
	inline constexpr std::string_view kLoginAttemptsKindLogin = "login";
	inline constexpr std::string_view kLoginAttemptsKindVerify = "verify";
	inline constexpr std::string_view kLoginAttemptsKindRemember = "remember";

	void MakeLoginAttemptsTable(DatabaseInfo databaseInfo);

	// Composite indexes for the per-email and per-IP failure-count
	// queries. Without these, the `SELECT count(*) FROM login_attempts
	// WHERE ...` queries the gate runs on every login go sequential and
	// turn into a DoS in their own right under attack load. Called
	// from create_database.cpp::CreateTables right after CreateTable.
	void CreateLoginAttemptsIndexes(Transaction& transaction);

}  // namespace DbSchema
