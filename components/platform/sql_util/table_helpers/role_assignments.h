#pragma once

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class RoleAssignments {
public:
    explicit RoleAssignments(DatabaseHelper databaseHelper);

    int64_t AddRoleAssignment(Transaction& transaction, int64_t personId, int64_t roleId);

    KeyValueTable GetRoleAssignments(Transaction& transaction, int64_t id) const;

    KeyValueTableArray GetRoleAssignmentsForPerson(Transaction& transaction, int64_t personId) const;
    KeyValueTableArray GetRoleAssignmentsForRole(Transaction& transaction, int64_t roleId) const;

    KeyValueTableArray GetRoleAssignments(Transaction& transaction) const;

    void DeleteRoleAssignment(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers