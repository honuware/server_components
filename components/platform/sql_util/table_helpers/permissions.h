#pragma once

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class Permissions {
public:
    explicit Permissions(DatabaseHelper databaseHelper);

    int64_t AddPermission(Transaction& transaction, std::string_view name, std::string_view description);

    KeyValueTable GetPermission(Transaction& transaction, int64_t id) const;
    KeyValueTable GetPermission(Transaction& transaction, std::string_view name) const;

    KeyValueTableArray GetPermissions(Transaction& transaction) const;

    // §10.2 — the customer-facing tier permissions (is_pricing_eligible = true),
    // used to populate the product pricing matrix and class-access pickers.
    KeyValueTableArray GetPricingEligiblePermissions(Transaction& transaction) const;

    void SetName(Transaction& transaction, int64_t id, std::string_view name);
    void SetDescription(Transaction& transaction, int64_t id, std::string_view description);
    void SetPricingEligible(Transaction& transaction, int64_t id, bool eligible);

    void DeletePermission(Transaction& transaction, int64_t id);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers