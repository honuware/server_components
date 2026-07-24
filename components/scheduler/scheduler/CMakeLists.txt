# The scheduler engine sources. Brand-free; namespace Scheduler. The engine
# consumes an arbitrary std::vector<Scheduler::ScheduledJob>, maintains one
# authenticated ApiClient per distinct target, and links honuware_foundation ONLY
# (util/http/http_client, util/logging, util/json_value). BOOST is kept explicit
# (foundation also provides it transitively) — the engine uses boost::asio
# io_context / steady_timer / signal_set directly. Internal includes are bare
# same-dir (api_client.h, scheduled_job.h, ...), matching the honuware convention;
# consumers use the `scheduler/` prefix via this component's include root.
target_sources(honuware_scheduler
    PRIVATE
    scheduled_job.h
    api_client.h
    api_client.cpp
    job_scheduler.h
    job_scheduler.cpp
    scheduler.h
    scheduler.cpp
)

# The engine's own tests build into the component test-case bag (honuware_tests),
# so they run in honuware's standalone runner AND in a consuming app's suite. They
# use foundation's HTTP client test double (util/http/http_client_test_util.h),
# which ships in honuware_testing — reachable because honuware_tests links it.
target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    api_client_test.cpp
    job_scheduler_test.cpp
    scheduler_test.cpp
)

target_link_libraries(honuware_scheduler PUBLIC honuware_foundation ${BOOST_LIB})
