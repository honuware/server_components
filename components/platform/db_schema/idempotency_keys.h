#pragma once

#include "sql_util/schema/database_info.h"

namespace DbSchema {

	inline constexpr std::string_view kIdempotencyKeysTable = "idempotency_keys";
	inline constexpr std::string_view kIdempotencyKeysId = "id";
	inline constexpr std::string_view kIdempotencyKeysScope = "scope";
	inline constexpr std::string_view kIdempotencyKeysKey = "key";
	inline constexpr std::string_view kIdempotencyKeysEndpoint = "endpoint";
	inline constexpr std::string_view kIdempotencyKeysRequestHash = "request_hash";
	inline constexpr std::string_view kIdempotencyKeysStatus = "status";
	inline constexpr std::string_view kIdempotencyKeysResponseJson = "response_json";
	inline constexpr std::string_view kIdempotencyKeysCreatedUs = "created_us";
	inline constexpr std::string_view kIdempotencyKeysExpiresUs = "expires_us";

	void MakeIdempotencyKeysTable(DatabaseInfo databaseInfo);

}  // namespace DbSchema
