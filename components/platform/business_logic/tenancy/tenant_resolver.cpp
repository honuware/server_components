#include "tenant_resolver.h"

#include <string>
#include <utility>

#include "sql_util/table_helpers/tenants.h"

namespace Tenancy {
namespace {

TenantContext ContextFromRow(const TableHelpers::TenantRow& row) {
    TenantContext context;
    context.tenantId = row.id;
    context.siteKey = row.siteKey;
    context.databaseName = row.databaseName;
    context.displayName = row.displayName;
    context.status = row.status;
    context.maxConnections = row.maxConnections;
    return context;
}

}  // namespace

ControlDbTenantResolver::ControlDbTenantResolver(
    TransactionProviderPtr controlProvider, DatabaseHelper controlDatabaseHelper)
    : controlProvider_(std::move(controlProvider)),
      controlDatabaseHelper_(controlDatabaseHelper) {}

std::optional<TenantContext> ControlDbTenantResolver::Resolve(
    std::string_view siteKey) {
    const std::string key(siteKey);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }
    }

    std::optional<TenantContext> resolved;
    controlProvider_->RunInTransaction([&](Transaction& transaction) {
        TableHelpers::Tenants tenants(controlDatabaseHelper_);
        std::optional<TableHelpers::TenantRow> row =
            tenants.LookupBySiteKey(transaction, siteKey);
        if (row.has_value()) {
            resolved = ContextFromRow(*row);
        }
    });

    if (resolved.has_value()) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = *resolved;
    }
    return resolved;
}

void ControlDbTenantResolver::Invalidate(std::string_view siteKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(std::string(siteKey));
}

void ControlDbTenantResolver::InvalidateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

FixedTenantResolver::FixedTenantResolver(TenantContext context)
    : context_(std::move(context)) {}

std::optional<TenantContext> FixedTenantResolver::Resolve(
    std::string_view siteKey) {
    if (siteKey.empty() || siteKey == context_.siteKey) {
        return context_;
    }
    return std::nullopt;
}

void FixedTenantResolver::Invalidate(std::string_view) {}
void FixedTenantResolver::InvalidateAll() {}

}  // namespace Tenancy
