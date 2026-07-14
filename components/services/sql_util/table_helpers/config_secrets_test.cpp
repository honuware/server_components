#include "config_secrets.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"

namespace TableHelpers {
namespace {

using ::testing::UnorderedElementsAre;

TEST(ConfigSecretsTest, AddSecretBasic)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("ConfigSecretsTest.AddSecretBasic", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        ConfigSecrets configSecrets(databaseHelper);
        configSecrets.AddSecret(transaction, "test1", "value1");
        configSecrets.AddSecret(transaction, "test2", "value2");

        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test1"), "value1");
        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test2"), "value2");
        });
}

TEST(ConfigSecretsTest, AddSecretDuplicate)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("ConfigSecretsTest.AddSecretDuplicate", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        ConfigSecrets configSecrets(databaseHelper);
        configSecrets.AddSecret(transaction, "test1", "value1");
        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test1"), "value1");
        configSecrets.AddSecret(transaction, "test1", "value2");
        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test1"), "value2");
        });
}

TEST(ConfigSecretsTest, LookupSecretNotFound)
{
    TestDatabaseUtil testDatabaseUtil;
    testDatabaseUtil.RunInTransaction("ConfigSecretsTest.LookupSecretNotFound", [&](Transaction& transaction) {
        DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();

        ConfigSecrets configSecrets(databaseHelper);
        ASSERT_EQ(configSecrets.LookupSecret(transaction, "test1"), std::string());
        });
}

} // namespace {
} // namespace TableHelpers {