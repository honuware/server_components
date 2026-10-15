#include "production_transaction_provider.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_access/pooled_transaction_provider.h"
#include "sql_util/database_access/transaction.h"
#include "test/src/util/database_test_helper.h"

namespace {

// Reconnecting a dropped connection (Finishing website Phase 4.1). On 10/15/2026
// RDS hung up on an idle server connection after a host network restart, and
// the server kept the dead socket -- every request failed until it was
// restarted by hand. These tests drop a REAL connection the same way, by
// terminating its backend from a second connection, which is exactly what a
// connection the server closed looks like to libpq.

// The active test database. The providers here open their own real
// connections to it; nothing they run writes anything.
std::string ActiveTestDbName() {
    TestDatabaseUtil testDb;
    return testDb.GetDatabaseHelper().GetDatabaseName();
}

std::string BackendPid(TransactionProvider& provider) {
    std::string pid;
    provider.RunInTransaction([&](Transaction& transaction) {
        pid = transaction.RunSqlStatementReturningOneValue("SELECT pg_backend_pid()");
    });
    return pid;
}

// Terminates the backend serving `pid` from a separate connection, then waits
// until the server has really let it go, so the next statement sent on the
// killed connection cannot race the termination.
void TerminateBackend(const std::string& pid) {
    DatabaseHelper admin = MakeNoDatabaseHelper();
    admin.RunInTransaction("TerminateBackend", [&](Transaction& transaction) {
        EXPECT_EQ(transaction.RunSqlStatementReturningOneValue(
            "SELECT pg_terminate_backend($1::int)", pid), "t");
    });
    for (int attempt = 0; attempt < 250; ++attempt) {
        std::string remaining;
        admin.RunInTransaction("WaitForTermination", [&](Transaction& transaction) {
            remaining = transaction.RunSqlStatementReturningOneValue(
                "SELECT COUNT(*) FROM pg_stat_activity WHERE pid = $1::int", pid);
        });
        if (remaining == "0") {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ADD_FAILURE() << "backend " << pid << " was still alive after 5 seconds";
}

// RAII stdout capture: LogInfo()/LogWarning()/LogError() write to std::cout.
class CoutCapture {
public:
    CoutCapture() : original_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(original_); }
    CoutCapture(const CoutCapture&) = delete;
    CoutCapture& operator=(const CoutCapture&) = delete;
    std::string str() const { return buffer_.str(); }
private:
    std::stringstream buffer_;
    std::streambuf* original_;
};

// The 10/15 case: the connection died while nothing was using it. The next
// transaction's BEGIN is the first thing to touch it, finds it dead, and gets a
// fresh connection -- the caller never sees an error.
TEST(ProductionTransactionProviderTest, ReconnectsWhenTheConnectionDiedWhileIdle) {
    TransactionProviderPtr provider = MakeProductionTransactionProvider(
        MakeProductionDatabaseHelper(ActiveTestDbName()));
    const std::string firstPid = BackendPid(*provider);
    ASSERT_FALSE(firstPid.empty());

    TerminateBackend(firstPid);

    std::string secondPid;
    std::string captured;
    {
        CoutCapture capture;
        secondPid = BackendPid(*provider);
        captured = capture.str();
    }
    EXPECT_FALSE(secondPid.empty());
    EXPECT_NE(secondPid, firstPid) << "the transaction must run on a NEW backend";
    EXPECT_NE(captured.find("[database] event=reconnected"), std::string::npos)
        << "captured: " << captured;
    EXPECT_NE(captured.find("reason=begin_failed"), std::string::npos)
        << "captured: " << captured;
}

// Work that had already started when the connection died is NOT re-run: it may
// have done something outside the database (a Square charge, an email). It
// fails, and the transaction after it gets the fresh connection.
TEST(ProductionTransactionProviderTest, WorkInterruptedByADroppedConnectionIsNotRerun) {
    TransactionProviderPtr provider = MakeProductionTransactionProvider(
        MakeProductionDatabaseHelper(ActiveTestDbName()));

    int calls = 0;
    std::string killedPid;
    EXPECT_ANY_THROW(provider->RunInTransaction([&](Transaction& transaction) {
        ++calls;
        killedPid = transaction.RunSqlStatementReturningOneValue("SELECT pg_backend_pid()");
        TerminateBackend(killedPid);
        transaction.RunSqlStatementReturningOneValue("SELECT 1");
    }));
    EXPECT_EQ(calls, 1) << "the interrupted work must run exactly once";

    std::string nextPid;
    std::string captured;
    {
        CoutCapture capture;
        nextPid = BackendPid(*provider);
        captured = capture.str();
    }
    EXPECT_FALSE(nextPid.empty());
    EXPECT_NE(nextPid, killedPid);
    EXPECT_NE(captured.find("[database] event=reconnected"), std::string::npos)
        << "captured: " << captured;
}

// A healthy connection is left alone: no reconnect, the same backend serves
// every transaction.
TEST(ProductionTransactionProviderTest, KeepsAHealthyConnection) {
    TransactionProviderPtr provider = MakeProductionTransactionProvider(
        MakeProductionDatabaseHelper(ActiveTestDbName()));
    std::string captured;
    std::string firstPid;
    std::string secondPid;
    {
        CoutCapture capture;
        firstPid = BackendPid(*provider);
        secondPid = BackendPid(*provider);
        captured = capture.str();
    }
    EXPECT_EQ(firstPid, secondPid);
    EXPECT_EQ(captured.find("event=reconnected"), std::string::npos);
}

// A genuine error in the caller's work is not mistaken for a dead connection:
// it propagates, and the same connection serves the next transaction.
TEST(ProductionTransactionProviderTest, AnOrdinaryErrorDoesNotReconnect) {
    TransactionProviderPtr provider = MakeProductionTransactionProvider(
        MakeProductionDatabaseHelper(ActiveTestDbName()));
    const std::string firstPid = BackendPid(*provider);

    EXPECT_ANY_THROW(provider->RunInTransaction([](Transaction& transaction) {
        transaction.RunSqlStatementReturningOneValue("SELECT no_such_column_anywhere");
    }));

    EXPECT_EQ(BackendPid(*provider), firstPid);
}

// The site's own requests run on a pool of 1 (tenancy). Each pooled slot is a
// ProductionTransactionProvider, so the pool recovers the same way -- in place,
// without growing past its bound.
TEST(PooledTransactionProviderTest, PoolOfOneRecoversFromADroppedConnection) {
    PooledTransactionProvider pool(ActiveTestDbName(), /*maxConnections=*/1);
    const std::string firstPid = BackendPid(pool);
    ASSERT_FALSE(firstPid.empty());

    TerminateBackend(firstPid);

    const std::string secondPid = BackendPid(pool);
    EXPECT_FALSE(secondPid.empty());
    EXPECT_NE(secondPid, firstPid);
    EXPECT_EQ(pool.GetCreatedConnectionCount(), 1);
}

// ---- DatabaseHelper::ResetConnection ---------------------------------------

TEST(DatabaseHelperResetTest, ProductionHelperOpensAFreshConnection) {
    DatabaseHelper helper = MakeProductionDatabaseHelper(ActiveTestDbName());
    auto pidOf = [&] {
        std::string pid;
        helper.RunInTransaction("Pid", [&](Transaction& transaction) {
            pid = transaction.RunSqlStatementReturningOneValue("SELECT pg_backend_pid()");
        });
        return pid;
    };
    const std::string before = pidOf();

    helper.ResetConnection();

    const std::string after = pidOf();
    EXPECT_FALSE(after.empty());
    EXPECT_NE(after, before);
}

// Test and bootstrap helpers refuse rather than pretend: the test harness's
// connection carries every test's transaction, and silently swapping it would
// break the abort-at-the-end isolation every test relies on.
TEST(DatabaseHelperResetTest, OtherHelpersRefuseToReset) {
    TestDatabaseUtil testDb;
    DatabaseHelper harness = testDb.GetDatabaseHelper();
    EXPECT_THROW(harness.ResetConnection(), std::logic_error);
    DatabaseHelper noDatabase = MakeNoDatabaseHelper();
    EXPECT_THROW(noDatabase.ResetConnection(), std::logic_error);
}

}  // namespace
