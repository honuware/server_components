#pragma once

#include <string>

// Safety guard for destructive operations (drop-and-recreate database, etc.).
// Operators have to opt in by setting HONUWARE_ALLOW_DESTRUCTIVE=1 in the
// environment — anything else (unset, "0", "true", "yes", literally anything
// that isn't exactly "1") blocks the operation. Production env files never
// set the variable, so production is always blocked.
//
// Lives in util/ so the tests can run without dragging in the entire
// database_helper library.
//
// The canonical name is HONUWARE_*; the legacy KNOTTYYOGA_* name is still
// honored as a fallback during the componentization rename (Phase 1.1).
inline constexpr const char* kDestructiveAllowedEnvVar =
    "HONUWARE_ALLOW_DESTRUCTIVE";
inline constexpr const char* kDestructiveAllowedEnvVarLegacy =
    "KNOTTYYOGA_ALLOW_DESTRUCTIVE";

// True iff HONUWARE_ALLOW_DESTRUCTIVE (or legacy KNOTTYYOGA_ALLOW_DESTRUCTIVE)
// is exactly "1".
bool IsDestructiveAllowed();

// Throws std::runtime_error with an operator-facing message when the
// destructive operation is not allowed. Caller (typically `main` in
// `knottyyoga_database_helper`) prints the message to stderr and exits
// non-zero.
void EnsureDestructiveAllowed();
