#include "now_us.h"

namespace StoredProcedures {

namespace {

constexpr std::string_view kProcedure = R"SQL(
CREATE OR REPLACE FUNCTION now_us()
RETURNS bigint
LANGUAGE sql
AS $$
  SELECT (extract(epoch FROM clock_timestamp()) * 1000000)::bigint
$$;
)SQL";

} // namespace

void CreateNowUs(Transaction& transaction) {
    transaction.RunSqlStatement(kProcedure);
}

std::string GenerateNowUsSql() {
    return std::string(kProcedure);
}

} // namespace StoredProcedures

