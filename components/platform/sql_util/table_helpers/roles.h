#pragma once

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class Roles {
public:
    explicit Roles(DatabaseHelper databaseHelper);

    int64_t AddRole(Transaction& transaction, std::string_view name, std::string_view description);

    KeyValueTable GetRole(Transaction& transaction, int64_t id) const;
    KeyValueTable GetRole(Transaction& transaction, std::string_view name) const;

    KeyValueTableArray GetRoles(Transaction& transaction) const;

    void SetName(Transaction& transaction, int64_t id, std::string_view name);
    void SetDescription(Transaction& transaction, int64_t id, std::string_view description);

    void DeleteRole(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers