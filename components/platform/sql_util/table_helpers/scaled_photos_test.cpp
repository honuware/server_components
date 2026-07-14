#include "scaled_photos.h"

#include <gtest/gtest.h>

#include "photo_instances.h"
#include "source_photos.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"

namespace TableHelpers {
namespace {

// Helper to create a source photo with a photo instance, returns {photoInstanceId, sourcePhotoId}
std::pair<int64_t, int64_t> CreateTestSourcePhoto(
    Transaction& transaction, DatabaseHelper databaseHelper) {
    PhotoInstances photoInstances(databaseHelper);
    int64_t piId = photoInstances.AddPhotoInstance(
        transaction, "source_bytes", "jpeg", 2000, 1500);
    SourcePhotos sourcePhotos(databaseHelper);
    int64_t spId = sourcePhotos.AddSourcePhoto(transaction, piId);
    return {piId, spId};
}

TEST(ScaledPhotosTest, AddAndGetScaledPhoto) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AddAndGetScaledPhoto", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPiId = photoInstances.AddPhotoInstance(
            transaction, "scaled_bytes", "jpeg", 200, 150);

        ScaledPhotos scaledPhotos(databaseHelper);
        int64_t scaledId = scaledPhotos.AddScaledPhoto(
            transaction, spId, scaledPiId, 200, 150);
        ASSERT_GT(scaledId, 0);

        KeyValueTable row = scaledPhotos.GetScaledPhoto(transaction, scaledId);
        EXPECT_EQ(row.at("source_photo_id"), std::to_string(spId));
        EXPECT_EQ(row.at("photo_instance_id"), std::to_string(scaledPiId));
        EXPECT_EQ(row.at("width"), "200");
        EXPECT_EQ(row.at("height"), "150");
    });
}

TEST(ScaledPhotosTest, GetScaledPhotoBySourceAndDimensions) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetBySourceAndDimensions", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPiId = photoInstances.AddPhotoInstance(
            transaction, "cached_bytes", "jpeg", 400, 300);

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, scaledPiId, 400, 300);

        // Should find the cached version
        KeyValueTable found = scaledPhotos.GetScaledPhotoBySourceAndDimensions(
            transaction, spId, 400, 300);
        EXPECT_FALSE(found.empty());
        EXPECT_EQ(found.at("width"), "400");

        // Should not find non-existent dimensions
        KeyValueTable notFound = scaledPhotos.GetScaledPhotoBySourceAndDimensions(
            transaction, spId, 500, 500);
        EXPECT_TRUE(notFound.empty());
    });
}

TEST(ScaledPhotosTest, GetScaledPhotosBySourcePhotoId) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetBySourcePhotoId", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t pi1 = photoInstances.AddPhotoInstance(
            transaction, "small", "jpeg", 100, 75);
        int64_t pi2 = photoInstances.AddPhotoInstance(
            transaction, "medium", "jpeg", 400, 300);

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi1, 100, 75);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi2, 400, 300);

        KeyValueTableArray results =
            scaledPhotos.GetScaledPhotosBySourcePhotoId(transaction, spId);
        EXPECT_EQ(results.size(), 2u);
    });
}

TEST(ScaledPhotosTest, UpdateLastUsedAtUs) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("UpdateLastUsedAtUs", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPiId = photoInstances.AddPhotoInstance(
            transaction, "touch_test", "jpeg", 200, 150);

        ScaledPhotos scaledPhotos(databaseHelper);
        int64_t scaledId = scaledPhotos.AddScaledPhoto(
            transaction, spId, scaledPiId, 200, 150);

        KeyValueTable before = scaledPhotos.GetScaledPhoto(transaction, scaledId);
        int64_t beforeUs = std::stoll(before.at("last_used_at_us"));

        scaledPhotos.UpdateLastUsedAtUs(transaction, scaledId);

        KeyValueTable after = scaledPhotos.GetScaledPhoto(transaction, scaledId);
        int64_t afterUs = std::stoll(after.at("last_used_at_us"));
        EXPECT_GE(afterUs, beforeUs);
    });
}

TEST(ScaledPhotosTest, DeleteScaledPhotoCascadesToPhotoInstance) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteScaledPhotoCascades", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPiId = photoInstances.AddPhotoInstance(
            transaction, "delete_test", "jpeg", 150, 100);

        ScaledPhotos scaledPhotos(databaseHelper);
        int64_t scaledId = scaledPhotos.AddScaledPhoto(
            transaction, spId, scaledPiId, 150, 100);

        scaledPhotos.DeleteScaledPhoto(transaction, scaledId);

        // Scaled photo should be gone
        EXPECT_TRUE(scaledPhotos.GetScaledPhoto(transaction, scaledId).empty());
        // Photo instance should also be deleted
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, scaledPiId).empty());
    });
}

TEST(ScaledPhotosTest, DeleteScaledPhotosBySourcePhotoId) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteBySourcePhotoId", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t pi1 = photoInstances.AddPhotoInstance(
            transaction, "batch1", "jpeg", 100, 75);
        int64_t pi2 = photoInstances.AddPhotoInstance(
            transaction, "batch2", "jpeg", 200, 150);

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi1, 100, 75);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi2, 200, 150);

        ASSERT_EQ(scaledPhotos.GetScaledPhotosBySourcePhotoId(transaction, spId).size(), 2u);

        scaledPhotos.DeleteScaledPhotosBySourcePhotoId(transaction, spId);

        EXPECT_EQ(scaledPhotos.GetScaledPhotosBySourcePhotoId(transaction, spId).size(), 0u);
        // Photo instances should also be deleted
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, pi1).empty());
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, pi2).empty());
    });
}

TEST(ScaledPhotosTest, GetTotalScaledPhotoStorageBytes) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetTotalStorageBytes", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t pi1 = photoInstances.AddPhotoInstance(
            transaction, "abcde", "jpeg", 100, 75);      // 5 bytes
        int64_t pi2 = photoInstances.AddPhotoInstance(
            transaction, "1234567890", "jpeg", 200, 150); // 10 bytes

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi1, 100, 75);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi2, 200, 150);

        int64_t totalBytes = scaledPhotos.GetTotalScaledPhotoStorageBytes(transaction);
        EXPECT_EQ(totalBytes, 15);
    });
}

TEST(ScaledPhotosTest, DeleteOlderThanDeletesAgedRowsAndCascadesToInstances) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteOlderThanDeletesAgedRowsAndCascadesToInstances",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t freshPi = photoInstances.AddPhotoInstance(
            transaction, "fresh_bytes", "jpeg", 100, 75);
        int64_t agedPi1 = photoInstances.AddPhotoInstance(
            transaction, "aged1_bytes", "jpeg", 200, 150);
        int64_t agedPi2 = photoInstances.AddPhotoInstance(
            transaction, "aged2_bytes", "jpeg", 400, 300);

        ScaledPhotos scaledPhotos(databaseHelper);
        int64_t freshId = scaledPhotos.AddScaledPhoto(transaction, spId, freshPi, 100, 75);
        int64_t agedId1 = scaledPhotos.AddScaledPhoto(transaction, spId, agedPi1, 200, 150);
        int64_t agedId2 = scaledPhotos.AddScaledPhoto(transaction, spId, agedPi2, 400, 300);

        // Force two rows into the deep past. now_us() uses clock_timestamp()
        // and advances during the transaction, so a cutoff of "nowUs" would
        // also catch the freshly-inserted row whose default last_used_at_us
        // was set a few microseconds before the SELECT now_us(). Use a small
        // absolute cutoff (1000 µs) that's comfortably above the aged values
        // (1) and far below any real wall-clock timestamp.
        transaction.RunSqlStatement(
            "UPDATE scaled_photos SET last_used_at_us = 1 WHERE scaled_photo_id = $1",
            StringFromInt(agedId1));
        transaction.RunSqlStatement(
            "UPDATE scaled_photos SET last_used_at_us = 1 WHERE scaled_photo_id = $1",
            StringFromInt(agedId2));

        int64_t deleted = scaledPhotos.DeleteOlderThan(transaction, 1000);
        EXPECT_EQ(deleted, 2);

        // Fresh row is still present.
        KeyValueTable freshRow = scaledPhotos.GetScaledPhoto(transaction, freshId);
        EXPECT_FALSE(freshRow.empty());
        EXPECT_FALSE(photoInstances.GetPhotoInstance(transaction, freshPi).empty());

        // Aged rows and their underlying photo_instances were both deleted.
        EXPECT_TRUE(scaledPhotos.GetScaledPhoto(transaction, agedId1).empty());
        EXPECT_TRUE(scaledPhotos.GetScaledPhoto(transaction, agedId2).empty());
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, agedPi1).empty());
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, agedPi2).empty());
    });
}

TEST(ScaledPhotosTest, DeleteOlderThanNoAgedRowsReturnsZero) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteOlderThanNoAgedRowsReturnsZero",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPi = photoInstances.AddPhotoInstance(
            transaction, "still_warm", "jpeg", 100, 75);

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, scaledPi, 100, 75);

        int64_t nowUs = std::stoll(
            transaction.RunSqlStatementReturningOneValue("SELECT now_us()"));
        // Cutoff in the distant past — nothing qualifies.
        int64_t deleted = scaledPhotos.DeleteOlderThan(transaction, nowUs - 3600LL * 1000000LL);
        EXPECT_EQ(deleted, 0);
    });
}

TEST(ScaledPhotosTest, DeleteOlderThanBoundaryNotInclusive) {
    // last_used_at_us == cutoffUs must NOT be deleted (strictly less than).
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("DeleteOlderThanBoundaryNotInclusive",
        [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();
        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t scaledPi = photoInstances.AddPhotoInstance(
            transaction, "boundary", "jpeg", 100, 75);

        ScaledPhotos scaledPhotos(databaseHelper);
        int64_t scaledId = scaledPhotos.AddScaledPhoto(
            transaction, spId, scaledPi, 100, 75);

        transaction.RunSqlStatement(
            "UPDATE scaled_photos SET last_used_at_us = 1000 WHERE scaled_photo_id = $1",
            StringFromInt(scaledId));

        int64_t deleted = scaledPhotos.DeleteOlderThan(transaction, 1000);
        EXPECT_EQ(deleted, 0);
        EXPECT_FALSE(scaledPhotos.GetScaledPhoto(transaction, scaledId).empty());

        deleted = scaledPhotos.DeleteOlderThan(transaction, 1001);
        EXPECT_EQ(deleted, 1);
        EXPECT_TRUE(scaledPhotos.GetScaledPhoto(transaction, scaledId).empty());
        EXPECT_TRUE(photoInstances.GetPhotoInstance(transaction, scaledPi).empty());
    });
}

TEST(ScaledPhotosTest, GetScaledPhotosOrderedByLastUsed) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("GetOrderedByLastUsed", [&](Transaction& transaction) {

        DatabaseHelper databaseHelper = testDb.GetDatabaseHelper();

        auto [sourcePiId, spId] = CreateTestSourcePhoto(transaction, databaseHelper);

        PhotoInstances photoInstances(databaseHelper);
        int64_t pi1 = photoInstances.AddPhotoInstance(
            transaction, "old", "jpeg", 100, 75);
        int64_t pi2 = photoInstances.AddPhotoInstance(
            transaction, "new", "jpeg", 200, 150);

        ScaledPhotos scaledPhotos(databaseHelper);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi1, 100, 75);
        scaledPhotos.AddScaledPhoto(transaction, spId, pi2, 200, 150);

        KeyValueTableArray results =
            scaledPhotos.GetScaledPhotosOrderedByLastUsed(transaction, 1);
        EXPECT_EQ(results.size(), 1u);
    });
}

}  // namespace
}  // namespace TableHelpers
