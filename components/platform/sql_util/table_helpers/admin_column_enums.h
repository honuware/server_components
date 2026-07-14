#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminColumnEnums {
public:
    explicit AdminColumnEnums(DatabaseHelper databaseHelper);

    void AddAdminColumnEnum(
        Transaction& transaction,
        int64_t adminEnumId,
        int64_t adminColumnDataInfoId);
    bool GetAdminColumnEnumByColumnDataInfoId(
        Transaction& transaction,
        int64_t columnDataInfoId,
        KeyValueTable& keyValueTable);
    KeyValueTableArray GetAdminColumnEnums(Transaction& transaction);
    void DeleteAdminColumnEnum(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
