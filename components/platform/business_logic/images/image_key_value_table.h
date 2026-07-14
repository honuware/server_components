#pragma once

#include "image_info.h"
#include "sql_util/database_common.h"

namespace Images {

KeyValueTable SourcePhotoInfoToKeyValueTable(const SourcePhotoInfo& info);
KeyValueTable ScaledPhotoInfoToKeyValueTable(const ScaledPhotoInfo& info);
SourcePhotoInfo SourcePhotoInfoFromKeyValueTable(const KeyValueTable& table);
ScaledPhotoInfo ScaledPhotoInfoFromKeyValueTable(const KeyValueTable& table);

}  // namespace Images
