#include "tenant_resources.h"

#include <utility>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/pooled_transaction_provider.h"
#include "util/mail/mail_helper.h"

namespace Tenancy {

void TenantResources::EnsureServices(Transaction& transaction) {
    std::call_once(servicesOnceFlag_, [&]() {
        // Build the tenant MailHelper unless one was injected (tests) or there are
        // no tenant secrets to read SMTP config from (fall back to the global).
        if (!mailHelper && secretsHelper) {
            mailHelper = Mail::MakeMailHelper(transaction, secretsHelper);
        }
        // Let the app subclass build its own per-tenant services (Square).
        BuildAppServices(transaction);
    });
}

TransactionProviderPtr MakeTenantTransactionProvider(const TenantContext& context) {
    const int maxConnections =
        context.maxConnections > 0 ? static_cast<int>(context.maxConnections) : 1;
    return MakePooledTransactionProvider(context.databaseName, maxConnections);
}

TenantResourcesPtr MakeDefaultTenantResources(const TenantContext& context) {
    auto resources = std::make_shared<TenantResources>();
    resources->context = context;
    resources->databaseHelper = MakeProductionDatabaseHelper(context.databaseName);
    resources->transactionProvider = MakeTenantTransactionProvider(context);
    return resources;
}

void PopulateBaseTenantResources(
    TenantResources& resources,
    const TenantContext& context,
    Secrets::SecretsAtRestPtr secretsAtRest) {
    resources.context = context;
    resources.databaseHelper = MakeProductionDatabaseHelper(context.databaseName);
    resources.transactionProvider = MakeTenantTransactionProvider(context);
    resources.secretsHelper =
        Secrets::MakeSecretsHelper(*resources.databaseHelper, secretsAtRest);
}

TenantResourcesPtr MakeDefaultTenantResources(
    const TenantContext& context, Secrets::SecretsAtRestPtr secretsAtRest) {
    auto resources = std::make_shared<TenantResources>();
    PopulateBaseTenantResources(*resources, context, secretsAtRest);
    return resources;
}

namespace {

// The one-argument default factory, wrapped in a lambda because
// MakeDefaultTenantResources is now overloaded (Phase 4.1) and a bare
// &MakeDefaultTenantResources would be ambiguous.
TenantResourcesPtr DefaultFactory(const TenantContext& context) {
    return MakeDefaultTenantResources(context);
}

}  // namespace

TenantResourceRegistry::TenantResourceRegistry(Factory factory)
    : factory_(factory ? std::move(factory) : Factory(&DefaultFactory)) {}

TenantResourcesPtr TenantResourceRegistry::GetOrCreate(const TenantContext& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = resources_.find(context.tenantId);
    if (it != resources_.end()) {
        return it->second;
    }
    TenantResourcesPtr resources = factory_(context);
    resources_[context.tenantId] = resources;
    return resources;
}

void TenantResourceRegistry::Evict(int64_t tenantId) {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_.erase(tenantId);
}

}  // namespace Tenancy
