#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

    inline constexpr std::string_view kDeviceTokensTable = "device_tokens";
    inline constexpr std::string_view kDeviceTokensId = "id";
    inline constexpr std::string_view kDeviceTokensUuid = "uuid";
    inline constexpr std::string_view kDeviceTokensPersonId = "person_id";
    inline constexpr std::string_view kDeviceTokensSecretHash = "secret_hash";
    inline constexpr std::string_view kDeviceTokensCreatedAt = "created_at";
    inline constexpr std::string_view kDeviceTokensLastUsedAt = "last_used_at";
    inline constexpr std::string_view kDeviceTokensExpiresAt = "expires_at";
    inline constexpr std::string_view kDeviceTokensRevoked = "revoked";

    void MakeDeviceTokensTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema