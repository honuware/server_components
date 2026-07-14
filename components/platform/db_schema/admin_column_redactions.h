#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Phase 3.8 of the security review: column-level redact map.
	//
	// Rows in this table identify (table_name, column_name) pairs
	// whose values must never appear in JSON responses produced by
	// the generic CRUD layer (get_row, get_table_rows, etc.). The
	// canonical seed list lives in
	// database_helper/create_database.cpp::PopulateAdminColumnRedactions
	// and covers credential material — password_hash, device-token
	// secret_hash, email-verification token_hash, session uuid.
	//
	// Note: there is no foreign key on table_name. Redactions apply
	// to tables that are intentionally NOT exposed via the admin CRUD
	// list (sessions, device_tokens, email_verifications) — those
	// don't appear in admin_top_level_tables. The redact map is the
	// last line of defence for the case where an endpoint other than
	// the admin CRUD set ever returns one of those tables' rows.
	inline constexpr std::string_view kAdminColumnRedactionsTable = "admin_column_redactions";
	inline constexpr std::string_view kAdminColumnRedactionsId = "id";
	inline constexpr std::string_view kAdminColumnRedactionsTableName = "table_name";
	inline constexpr std::string_view kAdminColumnRedactionsColumnName = "column_name";

	void MakeAdminColumnRedactionsTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
