#include "util/env.h"

#include <cstdlib>

#include <gtest/gtest.h>

namespace Util {
namespace {

// Cross-platform setenv/unsetenv. Mirrors the helpers in destructive_guard_test
// / scheduler_config_test. (On Windows, _putenv_s(name, "") removes the var, so
// these tests avoid depending on a distinct "set but empty" state.)
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

// Unique names so no other code reads them; both cleared around each test.
constexpr const char* kPrimary = "HONUWARE_ENV_TEST_PRIMARY";
constexpr const char* kFallback = "HONUWARE_ENV_TEST_FALLBACK";

struct EnvScope {
    EnvScope() { clear(); }
    ~EnvScope() { clear(); }
    void clear() {
        UnsetEnv(kPrimary);
        UnsetEnv(kFallback);
    }
};

TEST(GetEnvWithFallbackTest, PrimaryWinsWhenBothSet) {
    EnvScope scope;
    SetEnv(kPrimary, "new-value");
    SetEnv(kFallback, "old-value");
    EXPECT_STREQ(GetEnvWithFallback(kPrimary, kFallback), "new-value");
}

TEST(GetEnvWithFallbackTest, UsesPrimaryWhenOnlyPrimarySet) {
    EnvScope scope;
    SetEnv(kPrimary, "new-value");
    EXPECT_STREQ(GetEnvWithFallback(kPrimary, kFallback), "new-value");
}

TEST(GetEnvWithFallbackTest, FallsBackWhenPrimaryUnset) {
    EnvScope scope;
    SetEnv(kFallback, "old-value");
    EXPECT_STREQ(GetEnvWithFallback(kPrimary, kFallback), "old-value");
}

TEST(GetEnvWithFallbackTest, NullWhenNeitherSet) {
    EnvScope scope;
    EXPECT_EQ(GetEnvWithFallback(kPrimary, kFallback), nullptr);
}

}  // namespace
}  // namespace Util
