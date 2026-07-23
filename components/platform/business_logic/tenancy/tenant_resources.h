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
#include "util/secrets/secrets_helper.h"
#include "util/mail/mail_helper.h"

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
    // Phase 4.1: the tenant's SecretsHelper, built from its OWN database plus the
    // GLOBAL at-rest master key (every tenant's config_secrets.value is encrypted
    // with the one HONUWARE_SECRET_KEY — §1.6 "stays global"). Null when tenancy
    // was not wired to build it (a bare registry, or a consumer that doesn't set
    // it), in which case EndpointAuthHelper::GetSecretsHelper() falls back to the
    // deployment's global SecretsHelper.
    Secrets::SecretsHelperPtr secretsHelper;
    // Phase 4.3: the tenant's MailHelper (SMTP config read from the tenant's own
    // secrets). Either injected (tests) or built once by EnsureServices(); null
    // means EndpointAuthHelper::GetMailHelper() falls back to the global.
    Mail::MailHelperPtr mailHelper;

    virtual ~TenantResources() = default;

    // Phase 4.2/4.3: builds the per-tenant services that need a live transaction —
    // the MailHelper here, plus whatever an app subclass adds in BuildAppServices
    // (knottyyoga's Square client). Runs at most ONCE per tenant (std::call_once),
    // caching on this shared object. EndpointAuthHelper::Initialize() calls it
    // inside its own session-init transaction, which completes BEFORE any endpoint
    // transaction — so this never re-enters the tenant's pool-of-1 connection. A
    // helper that was already injected (tests) is left as-is.
    void EnsureServices(Transaction& transaction);

protected:
    // Hook for app-derived resources to build app-specific per-tenant services that
    // need a transaction (knottyyoga's Square client — Phase 4.2). Called by
    // EnsureServices under the once-guard, inside the transaction, with
    // secretsHelper available. Default: nothing.
    virtual void BuildAppServices(Transaction& /*transaction*/) {}

private:
    std::once_flag servicesOnceFlag_;
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
// (its DatabaseHelper + pooled TransactionProvider). Leaves secretsHelper null —
// use the secretsAtRest overload to build per-tenant secrets.
TenantResourcesPtr MakeDefaultTenantResources(const TenantContext& context);

// Phase 4.1: same as above, but also builds the tenant's SecretsHelper over its
// own database using the GLOBAL at-rest master key (`secretsAtRest`). This is the
// factory the composition root wires in so per-request `GetSecretsHelper()` reads
// the resolved tenant's config_secrets. (The app swaps in its own factory in
// Phase 4.2 to also carry the per-tenant Square client.)
TenantResourcesPtr MakeDefaultTenantResources(
    const TenantContext& context, Secrets::SecretsAtRestPtr secretsAtRest);

// Phase 4.2: fills the base per-tenant wiring (DatabaseHelper + pooled
// TransactionProvider + SecretsHelper) into an ALREADY-CONSTRUCTED resources
// object. An app factory constructs its own derived type (knottyyoga's
// KnottyTenantResources) and calls this so it doesn't duplicate the base
// construction. MakeDefaultTenantResources is written on top of it.
void PopulateBaseTenantResources(
    TenantResources& resources,
    const TenantContext& context,
    Secrets::SecretsAtRestPtr secretsAtRest);

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
