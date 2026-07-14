#pragma once

#include <string>
#include <string_view>

#include "util/mail/tenant_branding.h"

namespace Auth {
namespace Mail {

std::string GenerateVerifyMailBody(
    std::string_view emailFrom,
    std::string_view encodedSecret,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink);

std::string GenerateVerifyMailBodyPrefix(
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink);

std::string GenerateVerifyMailBodySuffix(
    std::string_view emailFrom,
    std::string_view firstNameTo,
    std::string_view lastNameTo,
    const ::Mail::TenantBranding& branding,
    std::string_view apiLinkPrefix,
    std::string_view activationLink);

}  // namespace Mail {
}  // namespace Auth {
