#include <iostream>

#include <gtest/gtest.h>

#include "db_schema/make_framework_tables.h"
#include "sql_util/schema/database_info.h"
#include "test/src/util/global_database_test_support.h"

// Standalone honuware component test runner.
//
// The shared harness (honuware_testing) is application-agnostic: it takes the
// composed schema as an INJECTED input rather than calling any app composition
// root. This repo has no application, so we compose a FRAMEWORK-ONLY schema
// (MakeFrameworkTables — auth/identity, admin metadata, framework infra, photos)
// and hand it to the harness, exactly as an app test main would inject its own
// MakeDatabaseInfo. A dedicated database name keeps this isolated from any app's
// test database on the same Postgres instance.
int main(int argc, char** argv)
{
    DbSchema::DatabaseInfo databaseInfo("honuware_test");
    DbSchema::MakeFrameworkTables(databaseInfo);

    if(!GlobalDatabaseTestSupport::Initialize(databaseInfo)) {
        std::cout << "Failed to initialize GlobalDatabaseTestSupport." << std::endl;
        return -1;
    }
    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    GlobalDatabaseTestSupport::Shutdown();
    return ret;
}
