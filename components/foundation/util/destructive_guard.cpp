#include "destructive_guard.h"

#include <cstdlib>
#include <stdexcept>

#include "util/env.h"

bool IsDestructiveAllowed() {
    const char* value = Util::GetEnvWithFallback(
        kDestructiveAllowedEnvVar, kDestructiveAllowedEnvVarLegacy);
    if (value == nullptr) {
        return false;
    }
    return std::string(value) == "1";
}

void EnsureDestructiveAllowed() {
    if (IsDestructiveAllowed()) {
        return;
    }
    throw std::runtime_error(
        "Destructive database operation refused. The "
        "HONUWARE_ALLOW_DESTRUCTIVE environment variable must be exactly "
        "\"1\" to authorize dropping and recreating the database. This is a "
        "safety guard that protects production deploys from accidental "
        "data loss. In dev, set the variable in your .vs/launch.vs.json "
        "(see SERVER.md) or in the shell that invokes this binary.");
}
