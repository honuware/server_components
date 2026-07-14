#include "secrets_helper.h"

#include "secrets_at_rest.h"
#include "sql_util/table_helpers/config_secrets.h"

namespace Secrets {

namespace {

class SecretsHelperImpl : public SecretsHelper {
public:
    SecretsHelperImpl(
        TableHelpers::ConfigSecrets configSecrets,
        SecretsAtRestPtr secretsAtRest)
        : configSecrets_(std::move(configSecrets)),
          secretsAtRest_(std::move(secretsAtRest)) {}

    std::string LookupSecret(
        Transaction& transaction, std::string_view name) const override {
        std::string raw = configSecrets_.LookupSecret(transaction, name);
        if (!secretsAtRest_) return raw;
        // Phase 8.1: Decrypt is a passthrough for legacy plaintext
        // values, so rows that haven't been migrated yet still
        // return their plaintext.
        return secretsAtRest_->Decrypt(raw);
    }

    void AddSecret(
        Transaction& transaction,
        std::string_view name,
        std::string_view value) override {
        if (secretsAtRest_) {
            // Phase 8.1: encrypt before writing so the plaintext
            // never lands in the database.
            std::string encrypted = secretsAtRest_->Encrypt(value);
            configSecrets_.AddSecret(transaction, name, encrypted);
            return;
        }
        configSecrets_.AddSecret(transaction, name, value);
    }

private:
    mutable TableHelpers::ConfigSecrets configSecrets_;
    SecretsAtRestPtr secretsAtRest_;
};

} // anonymous namespace

SecretsHelperPtr MakeSecretsHelper(
    DatabaseHelper databaseHelper,
    SecretsAtRestPtr secretsAtRest) {
    return std::make_shared<SecretsHelperImpl>(
        TableHelpers::ConfigSecrets(databaseHelper),
        std::move(secretsAtRest));
}

}  // namespace Secrets


