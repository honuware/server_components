#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction.h"
#include "sql_util/schema/database_info.h"

// H2 extraction (create_database framework/app split).
//
// These two functions are the framework halves a consuming app's create_database
// composes with its own app halves, instead of copying ~3,000 lines of framework
// DDL + seed. They cover ONLY honuware framework concerns (identity/auth, admin
// metadata, framework infra, photos); the app supplies its own tables + seed
// (app roles/permissions, admin users, app admin metadata, the scheduler service
// account, etc.).

// Creates the honuware framework tables (the MakeFrameworkTables set) in FK order,
// plus the three framework indexes (permission_implications, login_attempts,
// auth_events). `databaseInfo` must carry the framework table definitions (a
// composed framework++app DatabaseInfo works — only the framework subset is
// created here). Mirrors the framework subset of an app's CreateTables.
void CreateFrameworkTables(
    Transaction& transaction, DbSchema::DatabaseInfo databaseInfo);

// The table names CreateFrameworkTables builds, in FK / creation order.
//
// Exposed so a test can diff it against MakeFrameworkTables. Those two have to
// agree; for `site_assets` they silently did not, and because nothing compared
// them the table was registered-but-never-created for its whole life. A list is
// diffable in a way that a sequence of calls is not.
const std::vector<std::string_view>& FrameworkTableCreationOrder();

// Seeds the honuware framework baseline into the just-created framework tables:
//   - framework config-secret defaults (Secrets::Values::FillInSecretsStringView),
//   - the base Administrator + User roles,
//   - the admin_portal + staff_access permissions and the Administrator grants,
//   - the permission_implications allowed_tables entry,
//   - photo support for `people`,
//   - the security column redactions (password_hash, secret_hash, token_hash, uuid),
//   - the framework tables' admin metadata: top-level + nested registry, column +
//     table friendly names, table display templates, and column data-info.
// A consuming app composes this with its own app seed. Roles/permissions are
// referenced by NAME by the framework code, so their ids may differ per consumer.
void PopulateFrameworkTables(
    Transaction& transaction, DatabaseHelper databaseHelper,
    DbSchema::DatabaseInfo databaseInfo);
