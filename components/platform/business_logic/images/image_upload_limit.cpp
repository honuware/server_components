#include "business_logic/images/image_upload_limit.h"

#include <charconv>
#include <stdexcept>
#include <system_error>

#include "util/secrets/secret_keys.h"

namespace Images {

int64_t LoadImageUploadLimit(
    Transaction& transaction, const Secrets::SecretsHelperPtr& secrets) {
    const std::string stored =
        secrets->LookupSecret(transaction, Secrets::kImageMaxUploadBytes);
    int64_t bytes = 0;
    const char* end = stored.data() + stored.size();
    const auto [stop, error] = std::from_chars(stored.data(), end, bytes);
    if (error != std::errc() || stop != end || bytes <= 0) {
        return kImageUploadLimitDefaultBytes;
    }
    return bytes;
}

std::string ValidateImageUploadLimit(int64_t bytes) {
    if (bytes < kImageUploadLimitMinimumBytes || bytes > kImageUploadLimitMaximumBytes) {
        return "The upload limit must be from " +
               std::to_string(kImageUploadLimitMinimumBytes / kBytesPerMegabyte) + " to " +
               std::to_string(kImageUploadLimitMaximumBytes / kBytesPerMegabyte) + " MB.";
    }
    return "";
}

void SaveImageUploadLimit(
    Transaction& transaction, const Secrets::SecretsHelperPtr& secrets, int64_t bytes) {
    const std::string problem = ValidateImageUploadLimit(bytes);
    if (!problem.empty()) {
        throw std::invalid_argument(problem);
    }
    secrets->AddSecret(transaction, Secrets::kImageMaxUploadBytes, std::to_string(bytes));
}

}  // namespace Images
