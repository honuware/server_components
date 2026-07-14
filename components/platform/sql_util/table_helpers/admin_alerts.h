#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class AdminAlerts
{
public:
    AdminAlerts(DatabaseHelper databaseHelper);
    AdminAlerts(const AdminAlerts&) = default;
    AdminAlerts& operator=(const AdminAlerts&) = default;
    ~AdminAlerts() = default;

    void AddAdminAlert(Transaction& transaction, std::string_view description);
    void DeleteAdminAlert(Transaction& transaction, int64_t alertId);
    KeyValueTableArray GetAdminAlerts(Transaction& transaction, int64_t microsAlertWindow);

    // Phase 9.2 of the security review: digest-pull. Returns rows with
    // `created_at > sinceUs` (strict — so the caller can stamp
    // `last_notified_us = max(created_at)` after sending and be
    // guaranteed never to re-send the same row). Ordered oldest-first
    // so the digest email reads in chronological order.
    KeyValueTableArray GetAdminAlertsSince(
        Transaction& transaction, int64_t sinceUs);

private:
    DatabaseHelper databaseHelper_;
};

}  // namespace TableHelpers
