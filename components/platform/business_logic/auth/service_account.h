#pragma once

#include <string>
#include <string_view>

#include "sql_util/database_access/database_helper.h"

namespace Auth {

// Phase 3.2: the service-account email domain and the scheduler account email
// are a brand-specific, deploy-fixed identity, so they live app-side in
// business_logic/app_service_account_config.h (App::kServiceAccountEmailDomain /
// App::kSchedulerServiceAccountEmail) and are passed IN to the functions below.
// The framework keeps only the brand-free scheduler role shape + env-var name.

inline constexpr std::string_view kSchedulerServiceAccountFirstName =
    "Scheduler";
inline constexpr std::string_view kSchedulerServiceAccountLastName =
    "Service Account";

inline constexpr std::string_view kSchedulerRoleName = "scheduler";
inline constexpr std::string_view kSchedulerRoleDescription =
    "Scheduler service-account role. Least-privilege role for the scheduler "
    "helper executable (manage_subscriptions + manage_class_schedule).";

inline constexpr std::string_view kManageSubscriptionsPermission =
    "manage_subscriptions";

// Environment variable used by both the database-helper (to seed the row) and
// the scheduler helper (to log in). Single source of truth.
inline constexpr std::string_view kSchedulerServiceAccountPasswordEnvVar =
    "SCHEDULER_SERVICE_ACCOUNT_PASSWORD";

// Returns true if `email` ends with `domain` (e.g.
// App::kServiceAccountEmailDomain). Cheap pure helper — used both as a
// SQL-filter sanity check and in tests. The domain is a parameter so the
// framework carries no brand.
bool IsServiceAccountEmail(std::string_view email, std::string_view domain);

// Reads SCHEDULER_SERVICE_ACCOUNT_PASSWORD from the environment. Throws
// std::runtime_error with operator-facing instructions if unset/empty.
// Lives next to the creation helper so callers don't need to remember the
// env-var name; intentionally NOT called from EnsureSchedulerServiceAccount
// itself so unit tests can supply a password directly.
std::string ReadSchedulerServiceAccountPassword();

// Idempotently provisions the scheduler service account:
//   - Creates the `people` row for `schedulerEmail` (app-supplied, e.g.
//     App::kSchedulerServiceAccountEmail) with the password hashed and
//     email_verified set.
//   - Creates the `scheduler` role if absent.
//   - Links the role to the existing manage_subscriptions permission
//     (throws if the permission row is missing — PopulatePermissions in
//     create_database.cpp must run first).
//   - Assigns the role to the service-account person.
//
// If the person row already exists, the function returns immediately.
// Operators rotate the password by deleting the row and re-running the
// database helper.
void EnsureSchedulerServiceAccount(
    Transaction& transaction,
    DatabaseHelper databaseHelper,
    std::string_view schedulerEmail,
    std::string_view password);

}  // namespace Auth
