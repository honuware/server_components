#include "image_key_value_table.h"

#include "util/types.h"

namespace Images {

KeyValueTable SourcePhotoInfoToKeyValueTable(const SourcePhotoInfo& info) {
    KeyValueTable table;
    table["source_photo_id"] = StringFromInt(info.id);
    table["photo_instance_id"] = StringFromInt(info.photoInstanceId);
    table["type"] = info.type;
    table["width"] = std::to_string(info.width);
    table["height"] = std::to_string(info.height);
    table["created_at_us"] = StringFromInt(info.createdAtUs);
    table["last_updated_at_us"] = StringFromInt(info.lastUpdatedAtUs);
    return table;
}

KeyValueTable ScaledPhotoInfoToKeyValueTable(const ScaledPhotoInfo& info) {
    KeyValueTable table;
    table["scaled_photo_id"] = StringFromInt(info.id);
    table["source_photo_id"] = StringFromInt(info.sourcePhotoId);
    table["photo_instance_id"] = StringFromInt(info.photoInstanceId);
    table["type"] = info.type;
    table["width"] = std::to_string(info.width);
    table["height"] = std::to_string(info.height);
    table["created_at_us"] = StringFromInt(info.createdAtUs);
    table["last_used_at_us"] = StringFromInt(info.lastUsedAtUs);
    return table;
}

SourcePhotoInfo SourcePhotoInfoFromKeyValueTable(const KeyValueTable& table) {
    SourcePhotoInfo info;
    info.id = std::stoll(table.at("source_photo_id"));
    info.photoInstanceId = std::stoll(table.at("photo_instance_id"));
    info.type = table.at("type");
    info.width = std::stoi(table.at("width"));
    info.height = std::stoi(table.at("height"));
    info.createdAtUs = std::stoll(table.at("created_at_us"));
    info.lastUpdatedAtUs = std::stoll(table.at("last_updated_at_us"));
    return info;
}

ScaledPhotoInfo ScaledPhotoInfoFromKeyValueTable(const KeyValueTable& table) {
    ScaledPhotoInfo info;
    info.id = std::stoll(table.at("scaled_photo_id"));
    info.sourcePhotoId = std::stoll(table.at("source_photo_id"));
    info.photoInstanceId = std::stoll(table.at("photo_instance_id"));
    info.type = table.at("type");
    info.width = std::stoi(table.at("width"));
    info.height = std::stoi(table.at("height"));
    info.createdAtUs = std::stoll(table.at("created_at_us"));
    info.lastUsedAtUs = std::stoll(table.at("last_used_at_us"));
    return info;
}

}  // namespace Images
