#pragma once

#include "sql_util/database_access/transaction.h"
#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Permission-based class access redesign §3.1 — a directed implication
	// edge "holding `permission_id` also confers `implies_permission_id`".
	// The transitive closure over this DAG is what makes the membership tier
	// ladder work: granting `platinum` implies gold -> silver_partner_acro ->
	// silver, plus platinum's extra benefit permissions. Closure expansion
	// lives in the table helper (recursive CTE) and feeds
	// CatalogHelper::GetEffectivePermissionIds.
	inline constexpr std::string_view kPermissionImplicationsTable = "permission_implications";
	inline constexpr std::string_view kPermissionImplicationsId = "id";
	inline constexpr std::string_view kPermissionImplicationsPermissionId = "permission_id";
	inline constexpr std::string_view kPermissionImplicationsImpliesPermissionId = "implies_permission_id";

	void MakePermissionImplicationsTable(DatabaseInfo databaseInfo);

	// Index on permission_id supporting the closure-expansion query.
	void CreatePermissionImplicationsIndexes(Transaction& transaction);

}  // namespace DbSchema
