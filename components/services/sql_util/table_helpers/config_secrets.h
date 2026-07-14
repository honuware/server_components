#pragma once

#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class ConfigSecrets
{
public:
    ConfigSecrets(DatabaseHelper databaseHelper);
    ConfigSecrets(const ConfigSecrets&) = default;
    ConfigSecrets& operator=(const ConfigSecrets&) = default;
    ~ConfigSecrets() = default;

    void AddSecret(
        Transaction& transaction, 
        std::string_view name, 
        std::string_view value);
    std::string LookupSecret(Transaction& transaction, std::string_view name);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers

