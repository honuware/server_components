#include "sql_util/database_access/schema_check.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "sql_util/database_access/db_and_table_operations.h"
#include "test/src/util/database_test_helper.h"

// The startup schema check (CommunityFinder CM3 Phase 2 §8.2). The test
// database is built from the composed DatabaseInfo, so it is current; each
// test then adds what the build expects or the database has, in its own
// transaction.
namespace DbMeta {
    namespace {
        using ::testing::ElementsAre;
        using ::testing::HasSubstr;
        using ::testing::IsEmpty;

        constexpr std::string_view kEvents = "schema_check_events";

        // The build's schema: the composed one, plus a small events table.
        // A clone, so nothing here reaches the shared composed schema.
        DbSchema::DatabaseInfo BuildSchemaWithEventsTable(TestDatabaseUtil& testDatabaseUtil) {
            DbSchema::DatabaseInfo databaseInfo = testDatabaseUtil.GetDatabaseInfo().Clone();
            databaseInfo.AddTable(kEvents);
            databaseInfo.AddColumnPrimaryKey(kEvents, "event_id", DB_TYPE_SERIAL);
            databaseInfo.AddColumnSimple(kEvents, "title", DB_TYPE_STRING);
            return databaseInfo;
        }

        TEST(SchemaCheckTest, ACurrentDatabaseGivesNoWarning) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("SchemaCheckCurrent", [&](Transaction& transaction) {
                const MissingSchema missing =
                    FindMissingSchema(transaction, testDatabaseUtil.GetDatabaseInfo());
                EXPECT_TRUE(missing.Empty());
                EXPECT_EQ(DescribeMissingSchema(missing), "");
                EXPECT_FALSE(WarnIfDatabaseIsOlderThanBuild(
                    transaction, testDatabaseUtil.GetDatabaseInfo()));
            });
        }

        TEST(SchemaCheckTest, AMissingTableIsNamed) {
            TestDatabaseUtil testDatabaseUtil;
            const DbSchema::DatabaseInfo build = BuildSchemaWithEventsTable(testDatabaseUtil);
            testDatabaseUtil.RunInTransaction("SchemaCheckTable", [&](Transaction& transaction) {
                const MissingSchema missing = FindMissingSchema(transaction, build);
                // The table, once: its columns aren't listed again.
                EXPECT_THAT(missing.tables, ElementsAre(std::string(kEvents)));
                EXPECT_THAT(missing.columns, IsEmpty());
                EXPECT_EQ(DescribeMissingSchema(missing),
                    "The database is older than this build: missing table "
                    "schema_check_events. Run the database helper with --migrate, "
                    "or --recreate_database on a dev database.");
                EXPECT_TRUE(WarnIfDatabaseIsOlderThanBuild(transaction, build));
            });
        }

        TEST(SchemaCheckTest, AMissingColumnIsNamed) {
            TestDatabaseUtil testDatabaseUtil;
            DbSchema::DatabaseInfo build = BuildSchemaWithEventsTable(testDatabaseUtil);
            testDatabaseUtil.RunInTransaction("SchemaCheckColumn", [&](Transaction& transaction) {
                // The database has the table as it was; this build added a column.
                DbOps::CreateTable(transaction, build, kEvents);
                build.AddColumnNullable(kEvents, "video_url", DB_TYPE_STRING);
                const MissingSchema missing = FindMissingSchema(transaction, build);
                EXPECT_THAT(missing.tables, IsEmpty());
                EXPECT_THAT(missing.columns, ElementsAre("schema_check_events.video_url"));
                EXPECT_THAT(DescribeMissingSchema(missing),
                    HasSubstr("missing column schema_check_events.video_url."));
            });
        }

        TEST(SchemaCheckTest, SeveralMissingItemsShareOneMessage) {
            TestDatabaseUtil testDatabaseUtil;
            DbSchema::DatabaseInfo build = BuildSchemaWithEventsTable(testDatabaseUtil);
            testDatabaseUtil.RunInTransaction("SchemaCheckSeveral", [&](Transaction& transaction) {
                DbOps::CreateTable(transaction, build, kEvents);
                build.AddColumnNullable(kEvents, "video_url", DB_TYPE_STRING);
                build.AddColumnNullable(kEvents, "photo_credit", DB_TYPE_STRING);
                build.AddTable("schema_check_gallery_photos");
                build.AddColumnPrimaryKey(
                    "schema_check_gallery_photos", "gallery_photo_id", DB_TYPE_SERIAL);
                const MissingSchema missing = FindMissingSchema(transaction, build);
                EXPECT_THAT(missing.tables, ElementsAre("schema_check_gallery_photos"));
                EXPECT_THAT(missing.columns, ElementsAre(
                    "schema_check_events.video_url", "schema_check_events.photo_credit"));
                EXPECT_EQ(DescribeMissingSchema(missing),
                    "The database is older than this build: missing table "
                    "schema_check_gallery_photos; missing column "
                    "schema_check_events.video_url; missing column "
                    "schema_check_events.photo_credit. Run the database helper with "
                    "--migrate, or --recreate_database on a dev database.");
            });
        }

        TEST(SchemaCheckTest, ExtraTablesAndColumnsAreIgnored) {
            TestDatabaseUtil testDatabaseUtil;
            DbSchema::DatabaseInfo newer = BuildSchemaWithEventsTable(testDatabaseUtil);
            newer.AddColumnNullable(kEvents, "video_url", DB_TYPE_STRING);
            testDatabaseUtil.RunInTransaction("SchemaCheckExtra", [&](Transaction& transaction) {
                // The database has a column this build doesn't know about...
                DbOps::CreateTable(transaction, newer, kEvents);
                EXPECT_TRUE(FindMissingSchema(
                    transaction, BuildSchemaWithEventsTable(testDatabaseUtil)).Empty());
                // ...and, to the composed schema, a whole extra table.
                EXPECT_TRUE(FindMissingSchema(
                    transaction, testDatabaseUtil.GetDatabaseInfo()).Empty());
            });
        }

    }  // namespace
}  // namespace DbMeta
