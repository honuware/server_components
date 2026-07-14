#pragma once

#include <string>
#include <string_view>
#include <memory>

#include "sql_util/database_access/database_helper.h"

namespace Secrets {

// Forward declaration — SecretsAtRest now lives in this same util/secrets
// layer (Phase 1.3), so composing it here no longer pulls business_logic/auth
// into util (the old cycle). The full definition is in secrets_at_rest.h.
class SecretsAtRest;
using SecretsAtRestPtr = std::shared_ptr<SecretsAtRest>;

class SecretsHelper {
public:
    virtual ~SecretsHelper() = default;

    virtual std::string LookupSecret(
        Transaction& transaction, std::string_view name) const = 0;
    virtual void AddSecret(
        Transaction& transaction, 
        std::string_view name, 
        std::string_view value) = 0;

protected:
    SecretsHelper() = default;
    SecretsHelper(const SecretsHelper&) = default;
    SecretsHelper& operator=(const SecretsHelper&) = default;   
};

using SecretsHelperPtr = std::shared_ptr<SecretsHelper>;

// Phase 8.1 of the security review: production SecretsHelper now
// optionally wraps a SecretsAtRest so values are encrypted on write
// and decrypted on read. The encryption layer is fully internal —
// callers continue to see plaintext.
//
// `secretsAtRest = nullptr` preserves the pre-Phase-8 plaintext
// behavior. Used by the seed-time bootstrap in
// `database_helper/create_database.cpp::PopulateConfigSecrets` —
// the migration pass (Phase 8.1c) re-encrypts those rows in place
// at startup.
//
// `secretsAtRest` non-null is the production runtime path.
// AddSecret encrypts before writing; LookupSecret decrypts after
// reading. Legacy plaintext rows still decrypt correctly thanks to
// the passthrough behavior in SecretsAtRest::Decrypt.
SecretsHelperPtr MakeSecretsHelper(
    DatabaseHelper databaseHelper,
    SecretsAtRestPtr secretsAtRest = nullptr);

}  // namespace Secrets {