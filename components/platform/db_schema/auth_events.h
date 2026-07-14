#pragma once

#include "sql_util/database_access/transaction.h"
#include "sql_util/schema/database_info.h"
#include "sql_util/database_common.h"

namespace DbSchema {

	// Phase 9.1 of the security review: forensic-grade audit log of
	// authentication events. Distinct from `admin_alerts` (which is
	// operator-facing summary state) — `auth_events` is the
	// fine-grained per-action record an incident investigator pulls
	// when reconstructing what happened.
	//
	// Privacy note: this table holds PII (IP, user agent, sometimes
	// email indirectly via person_id). It is intentionally NOT
	// exposed via the generic CRUD endpoints — same pattern as
	// `login_attempts` and `sessions`.
	inline constexpr std::string_view kAuthEventsTable = "auth_events";
	inline constexpr std::string_view kAuthEventsId = "id";
	// One of the kind constants below. Kept as TEXT (not an enum
	// table) so adding new kinds is a code-only change.
	inline constexpr std::string_view kAuthEventsKind = "kind";
	// NULLABLE — failed-login on unknown email has no person to
	// associate with.
	inline constexpr std::string_view kAuthEventsPersonId = "person_id";
	// Best-effort client IP from Auth::ResolveClientIp (Phase 5.5).
	// Empty when not resolvable.
	inline constexpr std::string_view kAuthEventsIp = "ip";
	// Raw User-Agent header. Empty when absent.
	inline constexpr std::string_view kAuthEventsUserAgent = "user_agent";
	// now_us() at insert time.
	inline constexpr std::string_view kAuthEventsWhenUs = "when_us";
	// Free-form JSON payload — context that doesn't fit a column.
	// Examples: { "email": "..." } for login_failure where person_id
	// is null; { "from_role": "..." } for role_revoked. Empty string
	// when no detail is needed.
	inline constexpr std::string_view kAuthEventsDetailJson = "detail_json";

	// Kind constants. The set is closed at compile time so a typo at
	// a caller breaks the build instead of silently emitting an
	// unrecognised event.
	inline constexpr std::string_view kAuthEventsKindLoginSuccess = "login_success";
	inline constexpr std::string_view kAuthEventsKindLoginFailure = "login_failure";
	inline constexpr std::string_view kAuthEventsKindAccountLocked = "account_locked";
	inline constexpr std::string_view kAuthEventsKindLogout = "logout";
	inline constexpr std::string_view kAuthEventsKindPasswordChanged = "password_changed";
	inline constexpr std::string_view kAuthEventsKindEmailVerified = "email_verified";
	inline constexpr std::string_view kAuthEventsKindDeviceTokenUsed = "device_token_used";
	inline constexpr std::string_view kAuthEventsKindDeviceTokenRevoked = "device_token_revoked";
	inline constexpr std::string_view kAuthEventsKindRoleAssigned = "role_assigned";
	inline constexpr std::string_view kAuthEventsKindRoleRevoked = "role_revoked";

	void MakeAuthEventsTable(DatabaseInfo databaseInfo);

	// (person_id, when_us DESC) and (kind, when_us DESC) so an
	// incident investigator can pull either "everything that user
	// did in window X" or "every failed login in window X" without a
	// sequential scan. Called from create_database.cpp::CreateTables
	// right after CreateTable.
	void CreateAuthEventsIndexes(Transaction& transaction);

}  // namespace DbSchema
