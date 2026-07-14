#include "extensions.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test/src/util/database_test_helper.h"

namespace Extensions {
namespace {

// The bootstrap path (production and test) loads citext before any table is
// created. By the time TestDatabaseUtil hands us a transaction, citext must
// already exist in pg_extension or any DB_TYPE_CITEXT column would have
// failed to create.
TEST(ExtensionsTest, CitextExtensionPresentAfterBootstrap) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("CitextExtensionPresentAfterBootstrap",
        [&](Transaction& transaction) {
            std::string count = transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM pg_extension WHERE extname = $1",
                "citext");
            EXPECT_EQ(std::stoi(count), 1);
        });
}

TEST(ExtensionsTest, GenerateCreateExtensionsSqlIncludesCitext) {
    std::string sql = GenerateCreateExtensionsSql();
    EXPECT_NE(sql.find("CREATE EXTENSION IF NOT EXISTS citext"),
        std::string::npos)
        << "expected citext create-extension in DDL, got: " << sql;
}

}  // namespace
}  // namespace Extensions
