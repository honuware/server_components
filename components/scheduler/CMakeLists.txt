# honuware_scheduler — the generic scheduled-jobs engine (brand-free). Its include
# root is THIS directory, so `#include "scheduler/scheduler.h"` resolves here.
# scheduler links honuware_foundation ONLY: the engine consumes an arbitrary
# std::vector<Scheduler::ScheduledJob> and includes only foundation headers
# (util/http/http_client, util/logging, util/json_value) — nothing above
# foundation, so an upward include fails to compile. The app-side job catalog +
# scheduler main stay in the consumer (e.g. knottyyoga src/scheduler/).
target_include_directories(honuware_scheduler PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(scheduler)
