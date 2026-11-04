# H13: the scheduled jobs every site runs over the framework's own tables (the
# four cleanups and the admin-alerts digest), behind the framework's
# /api/admin/<job> endpoints. Moved up from the apps, which each carried a copy.
target_sources(honuware_platform PRIVATE
    platform_jobs.h
    platform_jobs.cpp
    admin_alerts_digest.h
    admin_alerts_digest.cpp
)

target_sources(${HONUWARE_TESTS_TARGET} PUBLIC
    platform_jobs_test.cpp
    admin_alerts_digest_test.cpp
)
