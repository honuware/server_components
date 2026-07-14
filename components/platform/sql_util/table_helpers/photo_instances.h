#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class PhotoInstances {
public:
    PhotoInstances(DatabaseHelper databaseHelper);
    PhotoInstances(const PhotoInstances&) = default;
    PhotoInstances& operator=(const PhotoInstances&) = default;
    ~PhotoInstances() = default;

    int64_t AddPhotoInstance(
        Transaction& transaction,
        std::string_view photoBytes,
        std::string_view type,
        int width,
        int height);
    KeyValueTable GetPhotoInstance(
        Transaction& transaction, int64_t id) const;
    void UpdatePhotoInstance(
        Transaction& transaction,
        int64_t id,
        std::string_view photoBytes,
        std::string_view type,
        int width,
        int height);
    void DeletePhotoInstance(
        Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
