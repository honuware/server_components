#pragma once

#include <cstdint>
#include <string>

namespace Tenancy {

// The resolved identity of a tenant for one request. An immutable value type,
// produced by a TenantResolver (from the control DB or a fixed configuration) and
// carried through the request edge into the per-tenant resource registry.
struct TenantContext {
    int64_t tenantId = 0;
    std::string siteKey;
    std::string databaseName;
    std::string displayName;
    // 'active' / 'suspended'. Lets the request edge distinguish an unknown site
    // (Resolve → empty) from a suspended one (Resolve → context with this set).
    std::string status;
    // Per-tenant connection-pool bound, carried here so the Phase 2 provider
    // factory needs no second control-DB lookup.
    int64_t maxConnections = 1;
};

}  // namespace Tenancy
