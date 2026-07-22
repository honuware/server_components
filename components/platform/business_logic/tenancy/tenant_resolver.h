#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "tenant_context.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction_provider.h"

namespace Tenancy {

// Resolves a CloudFront site key (the X-Honuware-Site header value) to a
// TenantContext. No request-path coupling here — Phase 3 wires the chosen
// implementation into the endpoint edge.
class TenantResolver {
public:
    virtual ~TenantResolver() = default;

    // Resolves a site key. Empty when the site is unknown. A suspended tenant
    // resolves WITH status == 'suspended' so the edge can tell suspended from
    // unknown; an active tenant resolves normally.
    virtual std::optional<TenantContext> Resolve(std::string_view siteKey) = 0;

    // Drops cached entries (call after onboarding / a status change). A no-op for
    // resolvers without a cache.
    virtual void Invalidate(std::string_view siteKey) = 0;
    virtual void InvalidateAll() = 0;
};

// Multi-tenant deployments: resolves against the control database's `tenants`
// table. Caches resolved contexts on read; Invalidate is driven from provisioning
// (Phase 5.1), so no TTL is needed.
class ControlDbTenantResolver : public TenantResolver {
public:
    ControlDbTenantResolver(
        TransactionProviderPtr controlProvider,
        DatabaseHelper controlDatabaseHelper);

    std::optional<TenantContext> Resolve(std::string_view siteKey) override;
    void Invalidate(std::string_view siteKey) override;
    void InvalidateAll() override;

private:
    TransactionProviderPtr controlProvider_;
    DatabaseHelper controlDatabaseHelper_;
    std::mutex mutex_;
    std::unordered_map<std::string, TenantContext> cache_;
};

// Single-tenant consumers + local dev: serves one fixed tenant with NO control
// database. Resolve succeeds for an empty site key (dev / single-tenant, where
// CloudFront injects no header) and for the tenant's own site key; any OTHER site
// key returns empty so the edge can reject a contradicting header. This is the
// zero-ceremony production path for single-tenant honuware consumers.
class FixedTenantResolver : public TenantResolver {
public:
    explicit FixedTenantResolver(TenantContext context);

    std::optional<TenantContext> Resolve(std::string_view siteKey) override;
    void Invalidate(std::string_view siteKey) override;
    void InvalidateAll() override;

private:
    TenantContext context_;
};

}  // namespace Tenancy
