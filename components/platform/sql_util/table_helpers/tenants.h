#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

// One row of the control database's `tenants` table.
struct TenantRow {
    int64_t id = 0;
    std::string siteKey;
    std::string databaseName;
    std::string displayName;
    std::string status;          // 'active' / 'suspended'
    int64_t maxConnections = 1;
};

// CRUD over the `tenants` control table. Constructed with the CONTROL database's
// DatabaseHelper; every method takes the active Transaction.
class Tenants {
public:
    explicit Tenants(DatabaseHelper databaseHelper);
    Tenants(const Tenants&) = default;
    Tenants& operator=(const Tenants&) = default;
    ~Tenants() = default;

    // Inserts a new tenant and returns its generated BIGSERIAL id. When `status`
    // is empty or `maxConnections` is <= 0 the column defaults ('active' / 1)
    // apply. Throws on a duplicate site_key / database_name (UNIQUE).
    int64_t Insert(Transaction& transaction, const TenantRow& row);

    // Looks up a tenant by site key regardless of status (so callers can tell a
    // suspended tenant from an unknown one). Empty when the site key is unknown.
    std::optional<TenantRow> LookupBySiteKey(
        Transaction& transaction, std::string_view siteKey) const;

    // All tenants whose status is 'active', ordered by id ascending.
    std::vector<TenantRow> ListActive(Transaction& transaction) const;

    // Sets a tenant's status by site key (also bumps updated_us).
    void SetStatus(
        Transaction& transaction, std::string_view siteKey, std::string_view status);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
