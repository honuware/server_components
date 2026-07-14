#include "destructive_guard.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

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

// RAII guard that restores BOTH the canonical HONUWARE_* and legacy
// KNOTTYYOGA_* names on exit (and scrubs both on entry) so individual tests can
// mutate them without leaking state into siblings or being perturbed by a
// legacy var set in the developer's launch environment.
class DestructiveEnvScope {
public:
    DestructiveEnvScope() {
        capture(kDestructiveAllowedEnvVar, hadPrimary_, priorPrimary_);
        capture(kDestructiveAllowedEnvVarLegacy, hadLegacy_, priorLegacy_);
        UnsetEnv(kDestructiveAllowedEnvVar);
        UnsetEnv(kDestructiveAllowedEnvVarLegacy);
    }
    ~DestructiveEnvScope() {
        restore(kDestructiveAllowedEnvVar, hadPrimary_, priorPrimary_);
        restore(kDestructiveAllowedEnvVarLegacy, hadLegacy_, priorLegacy_);
    }
private:
    static void capture(const char* name, bool& had, std::string& prior) {
        const char* val = std::getenv(name);
        had = (val != nullptr);
        if (had) prior.assign(val);
    }
    static void restore(const char* name, bool had, const std::string& prior) {
        if (had) SetEnv(name, prior.c_str());
        else UnsetEnv(name);
    }
    bool hadPrimary_ = false;
    bool hadLegacy_ = false;
    std::string priorPrimary_;
    std::string priorLegacy_;
};

// --- IsDestructiveAllowed ---

TEST(DestructiveGuardTest, FalseWhenUnset) {
    DestructiveEnvScope guard;
    EXPECT_FALSE(IsDestructiveAllowed());
}

TEST(DestructiveGuardTest, FalseWhenZero) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "0");
    EXPECT_FALSE(IsDestructiveAllowed());
}

TEST(DestructiveGuardTest, FalseWhenEmpty) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "");
    EXPECT_FALSE(IsDestructiveAllowed());
}

TEST(DestructiveGuardTest, FalseWhenNonOneStrings) {
    // Strict equality: only "1" allows. "true"/"yes"/"TRUE"/"01" all block.
    // This is intentional — anything that looks like a typo should fail
    // closed, not silently authorize destruction.
    DestructiveEnvScope guard;
    for (const char* value : { "true", "yes", "TRUE", "1 ", " 1", "01", "11", "y" }) {
        SetEnv(kDestructiveAllowedEnvVar, value);
        EXPECT_FALSE(IsDestructiveAllowed())
            << "value=" << value << " unexpectedly authorized";
    }
}

TEST(DestructiveGuardTest, TrueOnlyWhenExactlyOne) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "1");
    EXPECT_TRUE(IsDestructiveAllowed());
}

// Phase 1.1 rename: the legacy KNOTTYYOGA_* name still authorizes when the new
// HONUWARE_* name is unset, so existing deploy environments keep working.
TEST(DestructiveGuardTest, LegacyNameStillAuthorizes) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVarLegacy, "1");
    EXPECT_TRUE(IsDestructiveAllowed());
}

// The canonical name wins over the legacy one: HONUWARE="0" blocks even if the
// legacy var says "1".
TEST(DestructiveGuardTest, PrimaryNameWinsOverLegacy) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "0");
    SetEnv(kDestructiveAllowedEnvVarLegacy, "1");
    EXPECT_FALSE(IsDestructiveAllowed());
}

// --- EnsureDestructiveAllowed ---

TEST(DestructiveGuardTest, EnsureThrowsWhenUnset) {
    DestructiveEnvScope guard;
    EXPECT_THROW(EnsureDestructiveAllowed(), std::runtime_error);
}

TEST(DestructiveGuardTest, EnsureThrowsWhenZero) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "0");
    EXPECT_THROW(EnsureDestructiveAllowed(), std::runtime_error);
}

TEST(DestructiveGuardTest, EnsureDoesNotThrowWhenOne) {
    DestructiveEnvScope guard;
    SetEnv(kDestructiveAllowedEnvVar, "1");
    EXPECT_NO_THROW(EnsureDestructiveAllowed());
}

TEST(DestructiveGuardTest, EnsureMessageMentionsEnvVar) {
    // The operator-facing message must point at the variable so they know
    // what to fix. Verify the message contains the env-var name and the
    // word "set" (to communicate the action).
    DestructiveEnvScope guard;
    try {
        EnsureDestructiveAllowed();
        FAIL() << "Expected EnsureDestructiveAllowed to throw";
    } catch (const std::runtime_error& e) {
        std::string what = e.what();
        EXPECT_NE(what.find("HONUWARE_ALLOW_DESTRUCTIVE"), std::string::npos)
            << "Error message should name the env var: " << what;
        EXPECT_NE(what.find("\"1\""), std::string::npos)
            << "Error message should mention the required value: " << what;
    }
}

}  // namespace
