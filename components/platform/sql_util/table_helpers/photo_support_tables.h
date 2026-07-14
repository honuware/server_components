#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class PhotoSupportTables {
public:
    PhotoSupportTables(DatabaseHelper databaseHelper);
    PhotoSupportTables(const PhotoSupportTables&) = default;
    PhotoSupportTables& operator=(const PhotoSupportTables&) = default;
    ~PhotoSupportTables() = default;

    void AddPhotoSupportTable(
        Transaction& transaction, std::string_view tableName);
    bool IsPhotoSupportedForTable(
        Transaction& transaction, std::string_view tableName) const;
    KeyValueTableArray GetAllPhotoSupportTables(
        Transaction& transaction) const;
    void DeletePhotoSupportTable(
        Transaction& transaction, std::string_view tableName);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
