#include "make_framework_tables.h"

#include "admin_alerts.h"
#include "admin_column_data_info.h"
#include "admin_column_enums.h"
#include "admin_column_friendly_names.h"
#include "admin_column_redactions.h"
#include "admin_enum_values.h"
#include "admin_enums.h"
#include "admin_nested_tables.h"
#include "admin_table_display_template.h"
#include "admin_table_friendly_names.h"
#include "admin_table_permissions.h"
#include "admin_top_level_tables.h"
#include "allowed_tables.h"
#include "auth_events.h"
// Phase 3.1: config_secrets moved to honuware_services
// (components/services/db_schema/); reach it via the prefixed path, resolved
// through the services include root.
#include "db_schema/config_secrets.h"
#include "device_tokens.h"
#include "email_verifications.h"
#include "idempotency_keys.h"
#include "login_attempts.h"
#include "permission_implications.h"
#include "permissions.h"
#include "people.h"
#include "photos.h"
#include "role_assignments.h"
#include "role_permissions.h"
#include "roles.h"
#include "schema_migrations.h"
#include "sessions.h"

namespace DbSchema {

	void MakeFrameworkTables(DatabaseInfo databaseInfo) {
		// schema_migrations is owned by Migration::MigrationRunner. Registered
		// here so the standard MakeDatabaseInfo + CreateTables flow creates it
		// alongside everything else. The runner also bootstraps it via CREATE
		// TABLE IF NOT EXISTS for production hosts that predate this commit.
		MakeSchemaMigrationsTable(databaseInfo);

		MakeAdminAlertsTable(databaseInfo);
		MakeAdminTopLevelTablesTable(databaseInfo);
		MakeAdminNestedTablesTable(databaseInfo);
		MakeAdminColumnDataInfoTable(databaseInfo);
		MakeAdminColumnRedactionsTable(databaseInfo);
		MakeAdminEnumsTable(databaseInfo);
		MakeAdminEnumValuesTable(databaseInfo);
		MakeAdminColumnEnumsTable(databaseInfo);
		MakeAdminColumnFriendlyNamesTable(databaseInfo);
		MakeAdminTableFriendlyNamesTable(databaseInfo);
		MakeAdminTableDisplayTemplateTable(databaseInfo);
		MakeAllowedTablesTable(databaseInfo);

		// config_secrets is the secrets service's own storage table (ships with
		// honuware_services); registered in the framework stream because it is
		// framework infra and platform links services downward.
		MakeConfigSecretsTable(databaseInfo);

		MakePeopleTable(databaseInfo);
		MakeRolesTable(databaseInfo);
		MakePermissionsTable(databaseInfo);
		MakeAdminTablePermissionsTable(databaseInfo);
		MakeRoleAssignmentsTable(databaseInfo);
		MakeRolePermissionsTable(databaseInfo);
		// Permission-based class access redesign §3.1: permission implication
		// DAG. Both FKs point at permissions (above); no app dependency.
		MakePermissionImplicationsTable(databaseInfo);

		MakeSessionsTable(databaseInfo);
		MakeDeviceTokensTable(databaseInfo);
		MakeEmailVerificationsTable(databaseInfo);

		// Photo storage tables (framework). photos.h defines the whole cluster.
		MakePhotoSupportTablesTable(databaseInfo);
		MakePhotoInstancesTable(databaseInfo);
		MakeSourcePhotosTable(databaseInfo);
		MakeScaledPhotosTable(databaseInfo);
		MakeTableItemPhotosTable(databaseInfo);

		MakeIdempotencyKeysTable(databaseInfo);

		// Phase 5.1: brute-force defense bookkeeping. Read on every
		// login by LoginGate; written async via ThreadPool.
		MakeLoginAttemptsTable(databaseInfo);

		// Phase 9.1: forensic-grade auth event log. Written from
		// the auth endpoints (login, verify, logout, password-
		// change). Indexed by person_id and by kind.
		MakeAuthEventsTable(databaseInfo);
	}

}  // namespace DbSchema
