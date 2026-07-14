#include "migration_namespace.h"

#include <string>

#include <gtest/gtest.h>

namespace Migration {
namespace {

TEST(MigrationNamespaceTest, BuildsSlashSeparatedId) {
    EXPECT_EQ(NamespacedMigrationId("honuware", "0001_baseline"),
              "honuware/0001_baseline");
    EXPECT_EQ(NamespacedMigrationId(kFrameworkMigrationNamespace, "0002_x"),
              "honuware/0002_x");
    EXPECT_EQ(NamespacedMigrationId(kAppMigrationNamespace, "0002_x"),
              "app/0002_x");
}

TEST(MigrationNamespaceTest, SameLocalIdInDifferentNamespacesIsDistinct) {
    // The whole point: a framework "0001_baseline" and an app "0001_baseline"
    // must map to different schema_migrations ids so they can't collide.
    EXPECT_NE(
        NamespacedMigrationId(kFrameworkMigrationNamespace, "0001_baseline"),
        NamespacedMigrationId(kAppMigrationNamespace, "0001_baseline"));
}

TEST(MigrationNamespaceTest, FrameworkAndAppNamespacesDiffer) {
    EXPECT_NE(kFrameworkMigrationNamespace, kAppMigrationNamespace);
}

TEST(MigrationNamespaceTest, SeparatorIsNotPresentInNamespaceNames) {
    // The (namespace, localId) split back out of a recorded id is only
    // unambiguous if the namespace itself contains no separator.
    EXPECT_EQ(kFrameworkMigrationNamespace.find(kMigrationNamespaceSeparator),
              std::string_view::npos);
    EXPECT_EQ(kAppMigrationNamespace.find(kMigrationNamespaceSeparator),
              std::string_view::npos);
}

TEST(MigrationNamespaceTest, NamespacePrefixIsRecoverable) {
    std::string id =
        NamespacedMigrationId(kFrameworkMigrationNamespace, "0007_add_index");
    auto slash = id.find(kMigrationNamespaceSeparator);
    ASSERT_NE(slash, std::string::npos);
    EXPECT_EQ(id.substr(0, slash), kFrameworkMigrationNamespace);
    EXPECT_EQ(id.substr(slash + 1), "0007_add_index");
}

}  // namespace
}  // namespace Migration
