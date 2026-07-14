#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminEnumValues {
public:
    explicit AdminEnumValues(DatabaseHelper databaseHelper);

    void AddAdminEnumValue(
        Transaction& transaction,
        int64_t adminEnumId,
        std::string_view name,
        int value);
    KeyValueTableArray GetAdminEnumValuesByEnumId(
        Transaction& transaction,
        int64_t adminEnumId);
    void UpdateAdminEnumValue(
        Transaction& transaction,
        int64_t id,
        std::string_view name,
        int value);
    void DeleteAdminEnumValue(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
