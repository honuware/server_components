#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// Permission-based class access redesign §3.1 — CRUD + closure expansion for
// the permission-implication DAG.
class PermissionImplications {
public:
    explicit PermissionImplications(DatabaseHelper databaseHelper);
    PermissionImplications(const PermissionImplications&) = default;
    PermissionImplications& operator=(const PermissionImplications&) = default;
    ~PermissionImplications() = default;

    // Add edge "permissionId confers impliesPermissionId". Returns row id.
    int64_t AddImplication(
        Transaction& transaction,
        int64_t permissionId,
        int64_t impliesPermissionId);

    // Direct (one-hop) implications of a permission.
    KeyValueTableArray GetDirectImplications(
        Transaction& transaction, int64_t permissionId) const;

    KeyValueTableArray GetAll(Transaction& transaction) const;

    void DeleteImplication(Transaction& transaction, int64_t id);

    // Transitive closure of `seedPermissionIds`: the seeds plus every
    // permission reachable through implication edges. This is the linchpin the
    // §3.2 access gate / GetEffectivePermissionIds consumes so that holding
    // `platinum` effectively grants gold -> silver_partner_acro -> silver, etc.
    std::set<int64_t> ExpandClosure(
        Transaction& transaction,
        const std::vector<int64_t>& seedPermissionIds) const;

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
