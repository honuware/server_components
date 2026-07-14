#include "get_admin_alerts_in_window.h"

namespace StoredProcedures {

namespace {

constexpr std::string_view kProcedure = R"SQL(
-- Returns rows from admin_alerts in the last `micros_window` microseconds.

CREATE OR REPLACE FUNCTION get_admin_alerts_in_window(micros_window BIGINT)
RETURNS SETOF admin_alerts
LANGUAGE plpgsql
AS $$
DECLARE
  now_micros   BIGINT;
  lower_bound  BIGINT;
BEGIN
  IF micros_window IS NULL OR micros_window <= 0 THEN
    RAISE EXCEPTION 'micros_window must be a positive BIGINT (microseconds)';
  END IF;

  -- Use clock_timestamp() so "now" is the actual wall-clock time at call,
  -- not the transaction start time.
  now_micros  := (EXTRACT(EPOCH FROM clock_timestamp()) * 1000000)::BIGINT;
  lower_bound := now_micros - micros_window;

  RETURN QUERY
  SELECT *
  FROM admin_alerts
  WHERE created_at BETWEEN lower_bound AND now_micros
  ORDER BY created_at DESC;
END;
$$;
)SQL";

} // namespace

void CreateGetAdminAlertsInWindow(Transaction& transaction) {
    transaction.RunSqlStatement(kProcedure);
}

std::string GenerateGetAdminAlertsInWindowSql() {
    return std::string(kProcedure);
}

} // namespace StoredProcedures

