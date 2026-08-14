#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class TableItemPhotos {
public:
    TableItemPhotos(DatabaseHelper databaseHelper);
    TableItemPhotos(const TableItemPhotos&) = default;
    TableItemPhotos& operator=(const TableItemPhotos&) = default;
    ~TableItemPhotos() = default;

    int64_t AddTableItemPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId,
        int64_t sourcePhotoId);
    KeyValueTable GetTableItemPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId) const;
    KeyValueTable GetTableItemPhotoById(
        Transaction& transaction, int64_t id) const;
    void UpdateTableItemPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId,
        int64_t sourcePhotoId);
    void DeleteTableItemPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId);
    bool HasPhoto(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId) const;

    // The SOURCE image's `width`, `height` and `type` for an item, or an empty
    // table when it has no photo.
    //
    // Deliberately selects only those three columns: this answers "how big is
    // it" for a management LIST, and joining in the `photo` blob would pull a
    // full-size image per row into memory to print two numbers.
    KeyValueTable GetPhotoDimensions(
        Transaction& transaction,
        std::string_view tableName,
        int64_t tableItemId) const;

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
