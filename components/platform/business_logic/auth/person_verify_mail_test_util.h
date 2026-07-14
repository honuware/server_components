#pragma once

#include "person_verify_mail.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Auth {
namespace Mail {
namespace Test {

std::string GetEncodedToken(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    std::string_view mailBody);

std::string GenerateVerifyMailBodyPrefix(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo);

std::string GenerateVerifyMailBodySuffix(
    Secrets::Test::SecretsHelperTestPtr secrets,
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo);

}  // namespace Test {
}  // namespace Mail {
}  // namespace Auth {