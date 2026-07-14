#include "secrets_helper_test_util.h"

#include <utility>
#include "util/types.h"          // For KeyValueTable
#include "secrets_helper.h"
#include "secret_keys.h"
#include "secret_values.h"

namespace Secrets {
namespace Test {
namespace {

// The app/example-side defaults-filler injected via RegisterAppSecretDefaults.
// Empty until a composition root registers one — so this framework test double
// has NO compile- or link-time dependency on any app (the Phase-4 extraction
// seam; formerly this file #included business_logic/app_secret_values.h).
SecretDefaultsFiller& AppSecretDefaultsFiller() {
    static SecretDefaultsFiller filler;
    return filler;
}

class SecretsHelperTestImpl : public SecretsHelperTest {
public:
    SecretsHelperTestImpl() {
        Secrets::Values::FillInSecretsString([&](const std::string& key, const std::string& value) {
            AddSecretTest(key, value);
            });
        // App/example defaults (brand + domain) layered on top, if a composition
        // root registered them. In standalone honuware tests none is registered,
        // so the double carries framework defaults only.
        if (AppSecretDefaultsFiller()) {
            AppSecretDefaultsFiller()([&](const std::string& key, const std::string& value) {
                AddSecretTest(key, value);
                });
        }
        // Argon2id at MODERATE (the production default) takes ~250 ms per
        // hash. With dozens of tests running through PersonHelper that
        // adds up to seconds per test run for no security gain — tests
        // don't need crackproof hashes. Override to INTERACTIVE
        // (2 ops, 64 MB), which matches libsodium's interactive presets.
        // Tests that specifically want to verify the cost-parameter
        // plumbing override these via AddSecret/AddSecretTest at the
        // start of the test. Phase 2.5 of the security review.
        AddSecretTest(kAuthArgon2OpsLimit, "2");
        AddSecretTest(kAuthArgon2MemLimitKb, "65536");
    }
    ~SecretsHelperTestImpl() override = default;

    std::string LookupSecretTest(std::string_view name) const override {
        auto it = secrets_.find(std::string(name));
        if (it == secrets_.end()) {
            return {};
        }
        return it->second;
    }

    std::string LookupSecret (
        Transaction& transaction, std::string_view name) const override {
        return LookupSecretTest(name);
    }

    void AddSecretTest(
        std::string_view name, std::string_view value) override {
        secrets_[std::string(name)] = std::string(value);
    }

    void AddSecret(
        Transaction& transaction, 
        std::string_view name, 
        std::string_view value) override {
        AddSecretTest(name, value);
    }

private:
    // Simple in-memory store for test usage.
    KeyValueTable secrets_;
};

} // anonymous namespace

void RegisterAppSecretDefaults(SecretDefaultsFiller filler) {
    AppSecretDefaultsFiller() = std::move(filler);
}

SecretsHelperTestPtr MakeTestSecretsHelper() {
    return std::make_shared<SecretsHelperTestImpl>();
}

}  // namespace Test {
}  // namespace Secrets {

