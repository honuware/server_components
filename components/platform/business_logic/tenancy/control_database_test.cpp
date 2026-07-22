#include "control_database.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace Tenancy {
namespace {

// The two env vars ControlDatabaseName reads. Scrubbed on entry/exit so a value
// from one test (or an ambient value) cannot influence another.
constexpr const char* kControlDbEnvVars[] = {
    "HONUWARE_CONTROL_DB_NAME",
    "KNOTTYYOGA_CONTROL_DB_NAME",
};

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

std::optional<std::string> GetEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

// Snapshots + clears the control env vars on entry, restores on exit.
class EnvScope {
public:
    EnvScope() {
        for (const char* name : kControlDbEnvVars) {
            saved_.emplace_back(name, GetEnv(name));
            UnsetEnv(name);
        }
    }
    ~EnvScope() {
        for (const auto& entry : saved_) {
            if (entry.second) SetEnv(entry.first, entry.second->c_str());
            else UnsetEnv(entry.first);
        }
    }
private:
    std::vector<std::pair<const char*, std::optional<std::string>>> saved_;
};

TEST(ControlDatabaseTest, DefaultsToHonuwareControl) {
    EnvScope scope;
    EXPECT_EQ(ControlDatabaseName(), "honuware_control");
}

TEST(ControlDatabaseTest, HonuwareEnvOverride) {
    EnvScope scope;
    SetEnv("HONUWARE_CONTROL_DB_NAME", "my_control");
    EXPECT_EQ(ControlDatabaseName(), "my_control");
}

TEST(ControlDatabaseTest, LegacyEnvFallback) {
    EnvScope scope;
    SetEnv("KNOTTYYOGA_CONTROL_DB_NAME", "legacy_control");
    EXPECT_EQ(ControlDatabaseName(), "legacy_control");
}

TEST(ControlDatabaseTest, CanonicalWinsOverLegacy) {
    EnvScope scope;
    SetEnv("HONUWARE_CONTROL_DB_NAME", "canonical_control");
    SetEnv("KNOTTYYOGA_CONTROL_DB_NAME", "legacy_control");
    EXPECT_EQ(ControlDatabaseName(), "canonical_control");
}

TEST(ControlDatabaseTest, EmptyEnvUsesDefault) {
    // No legacy var set, so the empty-vs-absent platform difference does not
    // matter (see the Windows _putenv_s gotcha): both yield the default.
    EnvScope scope;
    SetEnv("HONUWARE_CONTROL_DB_NAME", "");
    EXPECT_EQ(ControlDatabaseName(), "honuware_control");
}

// EnsureControlDatabase() + MakeControlDatabaseHelper() create / connect to a
// real database, so they are exercised live in tenancy Phase 5.1 (--create-tenant)
// rather than in this transaction-rolled-back unit suite. The control schema
// itself is validated by MakeControlDatabaseInfoTest (structure) and TenantsTest
// (the table working in the composed primary test database).

}  // namespace
}  // namespace Tenancy
