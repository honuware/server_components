#include "make_control_database_info.h"

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "make_framework_tables.h"
#include "schema_migrations.h"
#include "tenants.h"
#include "sql_util/schema/database_info.h"

namespace DbSchema {
namespace {

bool Contains(const StringArray& tables, std::string_view name) {
    for (const std::string& table : tables) {
        if (table == name) return true;
    }
    return false;
}

TEST(MakeControlDatabaseInfoTest, ContainsExactlyTenantsAndSchemaMigrations) {
    DatabaseInfo info = MakeControlDatabaseInfo("test_honuware_control");
    StringArray tables = info.GetAllTables();

    std::set<std::string> unique(tables.begin(), tables.end());
    EXPECT_EQ(unique.size(), 2u);
    EXPECT_TRUE(Contains(tables, kTenantsTable));
    EXPECT_TRUE(Contains(tables, kSchemaMigrationsTable));
    EXPECT_EQ(info.GetDatabaseName(), "test_honuware_control");
}

TEST(MakeControlDatabaseInfoTest, FrameworkTablesDoNotIncludeTenants) {
    // The control-only `tenants` table must never leak into the framework schema
    // that every tenant database is built from.
    DatabaseInfo info("test_framework");
    MakeFrameworkTables(info);

    EXPECT_FALSE(Contains(info.GetAllTables(), kTenantsTable));
    // schema_migrations, by contrast, IS a framework table (present in both).
    EXPECT_TRUE(Contains(info.GetAllTables(), kSchemaMigrationsTable));
}

TEST(MakeControlDatabaseInfoTest, TenantsTableColumnsUniquesAndDefaults) {
    DatabaseInfo info("test_honuware_control");
    MakeTenantsTable(info);
    const TableInfo& table = info.GetTableInfo(kTenantsTable);

    EXPECT_TRUE(table.GetColumn(kTenantsId).IsPrimaryKey());
    EXPECT_TRUE(table.GetColumn(kTenantsSiteKey).IsUnique());
    EXPECT_TRUE(table.GetColumn(kTenantsDatabaseName).IsUnique());
    EXPECT_FALSE(table.GetColumn(kTenantsDisplayName).IsUnique());

    std::string defaultValue;
    ASSERT_TRUE(table.GetColumn(kTenantsStatus).IsDefault(defaultValue));
    EXPECT_EQ(defaultValue, "'active'");
    ASSERT_TRUE(table.GetColumn(kTenantsMaxConnections).IsDefault(defaultValue));
    EXPECT_EQ(defaultValue, "1");
    ASSERT_TRUE(table.GetColumn(kTenantsCreatedUs).IsDefault(defaultValue));
    EXPECT_EQ(defaultValue, "now_us()");
    ASSERT_TRUE(table.GetColumn(kTenantsUpdatedUs).IsDefault(defaultValue));
    EXPECT_EQ(defaultValue, "now_us()");
}

}  // namespace
}  // namespace DbSchema
