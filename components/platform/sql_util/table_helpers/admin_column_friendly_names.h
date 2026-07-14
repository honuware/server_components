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

class AdminColumnFriendlyNames
{
public:
    AdminColumnFriendlyNames(DatabaseHelper databaseHelper);
    AdminColumnFriendlyNames(const AdminColumnFriendlyNames&) = default;
    AdminColumnFriendlyNames& operator=(const AdminColumnFriendlyNames&) = default;
    ~AdminColumnFriendlyNames() = default;

    void AddAdminColumnFriendlyName(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view friendlyName);
    void DeleteAdminColumnFriendlyName(
        Transaction& transaction, 
        std::string_view table_name, 
        std::string_view columnName);
    std::string GetAdminColumnFriendlyName(
        Transaction& transaction,
        std::string_view tableName, 
        std::string_view columnName);
    KeyValueTableArray GetAdminColumnFriendlyNames(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers {