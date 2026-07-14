#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminEnums {
public:
    explicit AdminEnums(DatabaseHelper databaseHelper);

    int64_t AddAdminEnum(Transaction& transaction, std::string_view name);
    bool GetAdminEnum(
        Transaction& transaction,
        std::string_view name,
        KeyValueTable& keyValueTable);
    KeyValueTableArray GetAdminEnums(Transaction& transaction);
    void DeleteAdminEnum(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
