#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminNestedTables
{
public:
    AdminNestedTables(DatabaseHelper databaseHelper);
    AdminNestedTables(const AdminNestedTables&) = default;
    AdminNestedTables& operator=(const AdminNestedTables&) = default;
    ~AdminNestedTables() = default;

    void AddAdminNestedTable(Transaction& transaction, std::string_view tableName);
    void DeleteAdminNestedTable(Transaction& transaction, std::string_view tableName);
    bool IsNestedTable(Transaction& transaction, std::string_view tableName);
    StringArray GetAdminNestedTables(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
