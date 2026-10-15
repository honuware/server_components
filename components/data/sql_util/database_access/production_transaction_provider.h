#pragma once

#include <mutex>
#include <string_view>

#include "sql_util/database_access/database_helper.h"
#include "transaction_provider.h"

// Production transaction provider.
//
// CRITICAL: libpqxx connections are NOT thread-safe. Opening two
// `pqxx::work` instances concurrently on the same `pqxx::connection`
// throws "Started new transaction ... while transaction ... was still
// active" — and the entire process shares one connection (see
// `DatabaseHelperProd` in `database_helper.cpp`). Crow runs in
// `multithreaded()` mode, so a single page that issues parallel
// requests (the home-page carousel does four parallel image fetches
// over separate TCP connections) lands on different request threads
// and races.
//
// We serialize transactions with a process-wide mutex held for the
// duration of each `RunInTransaction` call. Concurrency benefit of
// multi-threading is preserved for non-DB work (cookie parsing,
// gzip, image scaling that happens AFTER the transaction commits,
// etc.) but DB access itself is serialized — fine for a low-traffic
// studio app. The right long-term fix is a connection pool; that's
// a separate, larger change and out of scope for the carousel fix
// that surfaced this race.
//
// RECONNECTS. The connection is opened once and can be dropped underneath
// the server (on 10/15/2026 a host network restart did exactly that; the site
// was down for hours with a dead socket). So a transaction that finds the
// connection closed, or whose BEGIN fails on a connection that turns out to
// be closed, reconnects first and logs `[database] event=reconnected`. Work
// that has already STARTED is never re-run: a mid-transaction failure
// propagates, and only the next transaction gets the fresh connection.
class ProductionTransactionProvider : public TransactionProvider {
public:
    ProductionTransactionProvider(DatabaseHelper databaseHelper);
    ProductionTransactionProvider(const ProductionTransactionProvider&) = delete;
    ProductionTransactionProvider& operator=(const ProductionTransactionProvider&) = delete;
    ~ProductionTransactionProvider() override;

    void RunInTransaction(TransactionProvider::DatabaseFunc databaseFunc) override;

private:
    // Replaces the connection and logs why; logs and rethrows if the database
    // cannot be reached. Called with connectionMutex_ held.
    void Reconnect(std::string_view reason);

    DatabaseHelper databaseHelper_;
    std::mutex connectionMutex_;
};

TransactionProviderPtr MakeProductionTransactionProvider(
    DatabaseHelper databaseHelper);
