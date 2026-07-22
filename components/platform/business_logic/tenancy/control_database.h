#pragma once

#include <string>

#include "sql_util/database_access/database_helper.h"

namespace Tenancy {

// The control database holds the `tenants` registry that maps site keys to
// tenant databases. Its name defaults to "honuware_control" and is overridable
// via the HONUWARE_CONTROL_DB_NAME env var (legacy KNOTTYYOGA_CONTROL_DB_NAME
// accepted as a fallback during the naming transition).
std::string ControlDatabaseName();

// A production DatabaseHelper connected to the control database.
//
// NOTE: MakeProductionDatabaseHelper honors the HONUWARE_DB_NAME override, so a
// process that sets HONUWARE_DB_NAME (to point at some other database) would have
// this control connection redirected too. A deployment running the control-DB
// resolver does not set HONUWARE_DB_NAME; a dedicated override that ignores it is
// a Phase 5.1 concern, when this runs inside a multi-database process.
DatabaseHelper MakeControlDatabaseHelper();

// Creates the control database if it does not already exist and creates its
// tables (idempotent — CREATE TABLE IF NOT EXISTS). NEVER drops an existing
// control database (it holds live tenant rows). Framework-callable; the app's
// database_helper CLI invokes it (tenancy plan Phase 5.1), and --create-tenant
// calls it before inserting a tenant row.
void EnsureControlDatabase();

}  // namespace Tenancy
