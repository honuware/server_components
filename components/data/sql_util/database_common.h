#ifndef __DATABASE_COMMON_H__
#define __DATABASE_COMMON_H__

// Hit linker issue that was solved with this. See:
//  https://stackoverflow.com/questions/42096935/error-while-compiling-libpqxx-code-example
//#define _GLIBCXX_USE_CXX11_ABI 0

#include <pqxx/pqxx>

#include "util/types.h"

enum DatabaseTypes {
	DB_TYPE_INT, DB_TYPE_SERIAL, DB_TYPE_BOOL, DB_TYPE_DOUBLE,
	DB_TYPE_STRING, DB_TYPE_JSON, DB_TYPE_BYTES, DB_TYPE_DATE,
	DB_TYPE_TIME, DB_TYPE_UUID, DB_TYPE_NULL, DB_TYPE_TIMESTAMP,
	DB_TYPE_TIMESTAMPTZ, DB_TYPE_BIGINT, DB_TYPE_BIGSERIAL,
	// Postgres user-defined types. Adding one of these to a column requires
	// the corresponding extension to be loaded at database bootstrap time
	// (see Extensions::CreateExtensions). Phase 1.6 of the security review.
	DB_TYPE_CITEXT
};

inline std::pair<std::string, std::string> DbPair(
	std::string_view key, std::string_view value) {
	return std::make_pair<std::string, std::string>(
		std::string(key), std::string(value));
}

// NOTE: the database name is no longer a framework constant. It is owned by
// the application (see business_logic/app_database_config.h) and passed as a
// parameter into DatabaseHelperInit / MakeProductionDatabaseHelper /
// MakeDatabaseInfo. This keeps sql_util brand-free for the honuware extraction
// and aligns with the multi-tenant plan (DB name is per-tenant).

#endif  // __DATABASE_COMMON_H__