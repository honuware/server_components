#include "tenant_resources.h"

#include <utility>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/pooled_transaction_provider.h"

namespace Tenancy {

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

TenantResourceRegistry::TenantResourceRegistry(Factory factory)
    : factory_(factory ? std::move(factory)
                       : Factory(&MakeDefaultTenantResources)) {}

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
