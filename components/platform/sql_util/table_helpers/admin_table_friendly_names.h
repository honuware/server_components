#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminTableFriendlyNames
{
public:
    AdminTableFriendlyNames(DatabaseHelper databaseHelper);
    AdminTableFriendlyNames(const AdminTableFriendlyNames&) = default;
    AdminTableFriendlyNames& operator=(const AdminTableFriendlyNames&) = default;
    ~AdminTableFriendlyNames() = default;

    void AddAdminTableFriendlyName(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view friendlyName,
        std::string_view description);
    void DeleteAdminTableFriendlyName(
        Transaction& transaction, 
        std::string_view tableName);
    bool GetAdminTableFriendlyName(
        Transaction& transaction, 
        std::string_view tableName, 
        KeyValueTable& keyValueTable);
    KeyValueTableArray GetAdminTableFriendlyNames(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers {