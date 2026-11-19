#pragma once

#include <cstdint>
#include <string>

#include "sql_util/database_access/transaction.h"
#include "util/secrets/secrets_helper.h"

// The largest image anyone may upload, in bytes: the `image_max_upload_bytes`
// secret, which the upload routes check (upload_photo, upload_user_photo).
// Until CommunityFinder CM3 Phase 2 §13 nothing could change it but a database
// edit, and the upload control couldn't know it. These are the one place its
// default and allowed range live, for the settings endpoints and their tests.
namespace Images {

inline constexpr int64_t kBytesPerMegabyte = 1024 * 1024;

// 10 MB, as the secret's seeded default (secret_values.cpp). A phone photo of
// a paper flyer is usually 2–5 MB.
inline constexpr int64_t kImageUploadLimitDefaultBytes = 10 * kBytesPerMegabyte;

// The range an admin may set (OQ21): at least 1 MB, so ordinary photos still
// go up; at most 50 MB, since more is too slow on a phone connection and the
// server holds each upload in memory while it handles it.
inline constexpr int64_t kImageUploadLimitMinimumBytes = 1 * kBytesPerMegabyte;
inline constexpr int64_t kImageUploadLimitMaximumBytes = 50 * kBytesPerMegabyte;

// The stored limit, which is what the upload routes enforce, even when it was
// set outside the range by hand. The default when it's unset or isn't a
// positive whole number.
int64_t LoadImageUploadLimit(
    Transaction& transaction, const Secrets::SecretsHelperPtr& secrets);

// "" when `bytes` is in range; otherwise why not, for a 400.
std::string ValidateImageUploadLimit(int64_t bytes);

// Stores a limit already checked with ValidateImageUploadLimit. Throws
// std::invalid_argument for one out of range, so nothing out of range is
// stored through here.
void SaveImageUploadLimit(
    Transaction& transaction, const Secrets::SecretsHelperPtr& secrets, int64_t bytes);

}  // namespace Images
