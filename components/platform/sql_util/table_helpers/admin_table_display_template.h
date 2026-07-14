#pragma once

#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"
#include "util/types.h"

namespace TableHelpers {

class AdminTableDisplayTemplate
{
public:
    AdminTableDisplayTemplate(DatabaseHelper databaseHelper);
    AdminTableDisplayTemplate(const AdminTableDisplayTemplate&) = default;
    AdminTableDisplayTemplate& operator=(const AdminTableDisplayTemplate&) = default;
    ~AdminTableDisplayTemplate() = default;

    std::string GetDisplayTemplate(Transaction& transaction, std::string_view tableName);
    void SetDisplayTemplate(Transaction& transaction, std::string_view tableName, std::string_view displayTemplate);
    void DeleteDisplayTemplate(Transaction& transaction, std::string_view tableName);
    KeyValueTable GetAllDisplayTemplates(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers
