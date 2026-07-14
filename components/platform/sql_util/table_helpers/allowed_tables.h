#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AllowedTables
{
public:
    AllowedTables(DatabaseHelper databaseHelper);
    AllowedTables(const AllowedTables &) = default;
    AllowedTables &operator=(const AllowedTables &) = default;
    ~AllowedTables() = default;

    void AddAllowedTable(Transaction& transaction, std::string_view tableName);
    void DeleteAllowedTable(
        Transaction& transaction, std::string_view tableName);
    bool IsTableAllowed(Transaction& transaction, std::string_view tableName);
    StringArray GetAllowedTables(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
