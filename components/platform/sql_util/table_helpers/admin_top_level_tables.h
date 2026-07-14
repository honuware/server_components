#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminTopLevelTables
{
public:
    AdminTopLevelTables(DatabaseHelper databaseHelper);
    AdminTopLevelTables(const AdminTopLevelTables&) = default;
    AdminTopLevelTables& operator=(const AdminTopLevelTables&) = default;
    ~AdminTopLevelTables() = default;

    void AddAdminTopLevelTable(Transaction& transaction, std::string_view tableName);
    void DeleteAdminTopLevelTable(Transaction& transaction, std::string_view tableName);
    bool IsTableAdminTopLevel(Transaction& transaction, std::string_view tableName);
    StringArray GetAdminTopLevelTables(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
