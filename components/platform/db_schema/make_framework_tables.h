#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	// Registers every honuware *framework* table (auth/identity, admin metadata,
	// framework infra, photos) into the shared DatabaseInfo. This is the platform
	// half of the old monolithic MakeDatabaseInfo; the app composition root
	// (MakeDatabaseInfo) calls this first, then MakeAppTables.
	//
	// Framework tables are added before app tables because foreign keys are
	// validated eagerly (DatabaseInfo::AddColumnForeignKeyRef looks up the parent
	// table): app tables may reference framework tables (people, permissions, ...)
	// as FK parents, but no framework table references an app table, so
	// framework-first is always a valid topological order.
	//
	// DatabaseInfo is a shared-pImpl handle, so passing by value mutates the same
	// underlying schema — matching the MakeXxxTable convention.
	void MakeFrameworkTables(DatabaseInfo databaseInfo);

}  // namespace DbSchema
