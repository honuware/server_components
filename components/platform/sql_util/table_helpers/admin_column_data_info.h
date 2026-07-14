#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminColumnDataInfo
{
public:
    AdminColumnDataInfo(DatabaseHelper databaseHelper);
    AdminColumnDataInfo(const AdminColumnDataInfo&) = default;
    AdminColumnDataInfo& operator=(const AdminColumnDataInfo&) = default;
    ~AdminColumnDataInfo() = default;

    void AddAdminColumnDataInfo(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view label,
        std::string_view hint = "",
        std::string_view placeHolder = "",
        std::string_view regex = "",
        std::string_view htmlInputType = "",
        std::string_view required = "",
        std::string_view maxLength = "",
        std::string_view defaultValue = "",
        std::string_view rows = "",
        std::string_view hidden = "",
        std::string_view readonly_ = "");
    void DeleteAdminColumnDataInfo(
        Transaction& transaction, 
        std::string_view table_name, 
        std::string_view columnName);
    bool GetAdminColumnDataInfo(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName,
        KeyValueTable& keyValueTable);
    KeyValueTableArray GetAdminColumnDataInfos(Transaction& transaction);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers