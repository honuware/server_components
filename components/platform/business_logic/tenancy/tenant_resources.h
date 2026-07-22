#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "tenant_context.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/transaction_provider.h"

namespace Tenancy {

// The per-tenant resources, built lazily on first use and cached for the tenant's
// lifetime. Phase 4 adds the per-tenant SecretsHelper / MailHelper / site config;
// the app-derived subclass (Phase 4.2) adds the per-tenant SquareClient — which is
// why this is a polymorphic base the app can extend. The framework never
// references any app service.
struct TenantResources {
    TenantContext context;
    // Optional because DatabaseHelper has no default constructor and a registry
    // built with a custom factory may not populate it (and the framework factory
    // only builds it lazily on first use). Phase 4 reads it to build the
    // per-tenant SecretsHelper / MailHelper.
    std::optional<DatabaseHelper> databaseHelper;
    TransactionProviderPtr transactionProvider;

    virtual ~TenantResources() = default;
};

using TenantResourcesPtr = std::shared_ptr<TenantResources>;

// Builds a tenant's TransactionProvider. THIS IS THE ONE PLACE the isolation model
// is chosen:
//   Model C (decided, Q1): a PooledTransactionProvider over the tenant's own
//     database, bounded by ctx.maxConnections (default 1 = today's
//     single-connection shape). No SQL-layer change; distinct tenants get distinct
//     pools and never block one another.
//   Model B (documented fallback, not built): a decorator issuing
//     `SET LOCAL search_path = <schema>` as the first statement of every
//     transaction on a shared connection. Swapping models stays a one-function
//     change here.
//
// NOTE — HONUWARE_DB_NAME footgun: the pool creates connections via
// MakeProductionDatabaseHelper, which honors the HONUWARE_DB_NAME override, so a
// control-mode process MUST NOT set HONUWARE_DB_NAME (it would misroute every
// tenant to one database). A name-forcing connection variant is a Phase 5.1 item.
TransactionProviderPtr MakeTenantTransactionProvider(const TenantContext& context);

// The default framework factory: builds a base TenantResources for a tenant
// (its DatabaseHelper + pooled TransactionProvider).
TenantResourcesPtr MakeDefaultTenantResources(const TenantContext& context);

// Lazily builds and caches one TenantResources per tenant (keyed by tenantId).
// Constructed with an app-supplied factory so the app can return a DERIVED type
// carrying app services (knottyyoga's adds the per-tenant SquareClient in Phase
// 4.2); the framework itself never knows about any app service. Defaults to the
// framework factory.
class TenantResourceRegistry {
public:
    using Factory = std::function<TenantResourcesPtr(const TenantContext&)>;

    explicit TenantResourceRegistry(Factory factory = nullptr);

    // Returns the tenant's resources, building them once on first request and
    // caching them for reuse.
    TenantResourcesPtr GetOrCreate(const TenantContext& context);

    // Drops a cached tenant's resources (e.g. on suspension / reconfiguration).
    void Evict(int64_t tenantId);

private:
    Factory factory_;
    std::mutex mutex_;
    std::unordered_map<int64_t, TenantResourcesPtr> resources_;
};

}  // namespace Tenancy
