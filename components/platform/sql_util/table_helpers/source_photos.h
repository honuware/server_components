#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class SourcePhotos {
public:
    SourcePhotos(DatabaseHelper databaseHelper);
    SourcePhotos(const SourcePhotos&) = default;
    SourcePhotos& operator=(const SourcePhotos&) = default;
    ~SourcePhotos() = default;

    int64_t AddSourcePhoto(
        Transaction& transaction,
        int64_t photoInstanceId);
    KeyValueTable GetSourcePhoto(
        Transaction& transaction, int64_t id) const;
    KeyValueTable GetSourcePhotoWithInstance(
        Transaction& transaction, int64_t id) const;
    void DeleteSourcePhoto(
        Transaction& transaction, int64_t id);
    void UpdateSourcePhotoTimestamp(
        Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
