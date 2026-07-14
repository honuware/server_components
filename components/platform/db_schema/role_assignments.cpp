#include "role_assignments.h"

namespace DbSchema {

    void MakeRoleAssignmentsTable(DatabaseInfo databaseInfo) {
        databaseInfo.AddTable(kRoleAssignmentsTable);
        databaseInfo.AddColumnPrimaryKey(
            kRoleAssignmentsTable,
            kRoleAssignmentsId,
            DB_TYPE_BIGSERIAL);
        // Foreign keys
        databaseInfo.AddColumnForeignKeyRef(
            kPeopleTable,
            kPeopleId,
            kRoleAssignmentsTable,
            kRoleAssignmentsPersonId);
        databaseInfo.AddColumnForeignKeyRef(
            kRolesTable,
            kRolesId,
            kRoleAssignmentsTable,
            kRoleAssignmentsRoleId);
        // A given (person, role) pair is meaningful at most once. Phase 1.4
        // of the security review.
        databaseInfo.AddUniqueConstraint(
            kRoleAssignmentsTable,
            kRoleAssignmentsPersonId,
            kRoleAssignmentsRoleId);
    }

}  // namespace DbSchema