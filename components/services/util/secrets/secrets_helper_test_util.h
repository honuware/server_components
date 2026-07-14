#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "secrets_helper.h"

namespace Secrets {
namespace Test {

// A defaults-filler receives an `addSecret(key, value)` callback and registers
// key→value defaults through it (same shape as Secrets::Values::FillInSecretsString
// and App::FillInAppSecretDefaultsString).
using SecretDefaultsFiller = std::function<void(
    std::function<void(const std::string&, const std::string&)>)>;

// Inject the app/example-side default secret VALUES applied on top of the
// framework defaults by every subsequently-constructed test secrets helper.
// This is the seam that keeps the framework test double app-free: the harness
// always loads Secrets::Values (framework) defaults; the composition root (e.g.
// the app test main) registers its brand/domain defaults here — mirroring the
// injected DbSchema::MakeDatabaseInfo in GlobalDatabaseTestSupport. Standalone
// honuware tests register nothing, so the double is framework-only. Call once
// before tests run; pass nullptr to clear. Not thread-safe (the test main runs
// single-threaded before RUN_ALL_TESTS).
void RegisterAppSecretDefaults(SecretDefaultsFiller filler);

class SecretsHelperTest : public SecretsHelper {
public:
    virtual ~SecretsHelperTest() = default;

    virtual std::string LookupSecretTest(std::string_view name) const = 0;
    virtual void AddSecretTest(
        std::string_view name, std::string_view value) = 0;

protected:
    SecretsHelperTest() = default;
    SecretsHelperTest(const SecretsHelperTest&) = default;
    SecretsHelperTest& operator=(const SecretsHelperTest&) = default;
};

using SecretsHelperTestPtr = std::shared_ptr<SecretsHelperTest>;

// Factory that returns an in-memory (non-database) implementation of SecretsHelper
// suitable for unit tests.
SecretsHelperTestPtr MakeTestSecretsHelper();

}  // namespace Test {
} // namespace Secrets