#pragma once

namespace Util {

// Reads the environment variable named `primary`; if that variable is not set,
// falls back to the variable named `fallback`. Returns nullptr when neither is
// set. A `primary` that is set but empty ("") still wins over `fallback` — an
// explicitly-set new name takes precedence, matching "read the new name first".
//
// Supports the HONUWARE_* env-var rename (componentization Phase 1.1): call
// with the new HONUWARE_* name as `primary` and the legacy KNOTTYYOGA_* name as
// `fallback` so old deploy environments keep working during the transition.
//
// Pure getenv wrapper — no logging — so it is safe to call from logging's own
// bootstrap (which resolves the log destination before any log sink exists).
const char* GetEnvWithFallback(const char* primary, const char* fallback);

}  // namespace Util
