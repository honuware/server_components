#include "image_key_value_table.h"

#include <gtest/gtest.h>

namespace Images {
namespace {

TEST(ImageKeyValueTableTest, SourcePhotoInfoRoundTrip) {
    SourcePhotoInfo original;
    original.id = 42;
    original.photoInstanceId = 100;
    original.type = "jpeg";
    original.width = 800;
    original.height = 600;
    original.createdAtUs = 1700000000000000;
    original.lastUpdatedAtUs = 1700000001000000;

    KeyValueTable kv = SourcePhotoInfoToKeyValueTable(original);
    SourcePhotoInfo restored = SourcePhotoInfoFromKeyValueTable(kv);

    EXPECT_EQ(restored.id, original.id);
    EXPECT_EQ(restored.photoInstanceId, original.photoInstanceId);
    EXPECT_EQ(restored.type, original.type);
    EXPECT_EQ(restored.width, original.width);
    EXPECT_EQ(restored.height, original.height);
    EXPECT_EQ(restored.createdAtUs, original.createdAtUs);
    EXPECT_EQ(restored.lastUpdatedAtUs, original.lastUpdatedAtUs);
}

TEST(ImageKeyValueTableTest, SourcePhotoInfoKeyNames) {
    SourcePhotoInfo info;
    info.id = 1;
    info.photoInstanceId = 2;
    info.type = "png";
    info.width = 640;
    info.height = 480;
    info.createdAtUs = 100;
    info.lastUpdatedAtUs = 200;

    KeyValueTable kv = SourcePhotoInfoToKeyValueTable(info);

    EXPECT_EQ(kv.at("source_photo_id"), "1");
    EXPECT_EQ(kv.at("photo_instance_id"), "2");
    EXPECT_EQ(kv.at("type"), "png");
    EXPECT_EQ(kv.at("width"), "640");
    EXPECT_EQ(kv.at("height"), "480");
    EXPECT_EQ(kv.at("created_at_us"), "100");
    EXPECT_EQ(kv.at("last_updated_at_us"), "200");
}

TEST(ImageKeyValueTableTest, ScaledPhotoInfoRoundTrip) {
    ScaledPhotoInfo original;
    original.id = 77;
    original.sourcePhotoId = 42;
    original.photoInstanceId = 200;
    original.type = "jpeg";
    original.width = 320;
    original.height = 240;
    original.createdAtUs = 1700000000000000;
    original.lastUsedAtUs = 1700000005000000;

    KeyValueTable kv = ScaledPhotoInfoToKeyValueTable(original);
    ScaledPhotoInfo restored = ScaledPhotoInfoFromKeyValueTable(kv);

    EXPECT_EQ(restored.id, original.id);
    EXPECT_EQ(restored.sourcePhotoId, original.sourcePhotoId);
    EXPECT_EQ(restored.photoInstanceId, original.photoInstanceId);
    EXPECT_EQ(restored.type, original.type);
    EXPECT_EQ(restored.width, original.width);
    EXPECT_EQ(restored.height, original.height);
    EXPECT_EQ(restored.createdAtUs, original.createdAtUs);
    EXPECT_EQ(restored.lastUsedAtUs, original.lastUsedAtUs);
}

TEST(ImageKeyValueTableTest, ScaledPhotoInfoKeyNames) {
    ScaledPhotoInfo info;
    info.id = 10;
    info.sourcePhotoId = 5;
    info.photoInstanceId = 20;
    info.type = "jpeg";
    info.width = 160;
    info.height = 120;
    info.createdAtUs = 300;
    info.lastUsedAtUs = 400;

    KeyValueTable kv = ScaledPhotoInfoToKeyValueTable(info);

    EXPECT_EQ(kv.at("scaled_photo_id"), "10");
    EXPECT_EQ(kv.at("source_photo_id"), "5");
    EXPECT_EQ(kv.at("photo_instance_id"), "20");
    EXPECT_EQ(kv.at("type"), "jpeg");
    EXPECT_EQ(kv.at("width"), "160");
    EXPECT_EQ(kv.at("height"), "120");
    EXPECT_EQ(kv.at("created_at_us"), "300");
    EXPECT_EQ(kv.at("last_used_at_us"), "400");
}

}  // namespace
}  // namespace Images
