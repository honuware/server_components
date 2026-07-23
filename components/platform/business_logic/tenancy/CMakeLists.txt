# Multi-tenancy control plane (honuware_platform). The control-DB schema/table
# lives in db_schema/ + sql_util/table_helpers/; this dir holds the control-DB
# access + the tenant resolver. No request-path coupling yet — Phase 3 wires it in.

target_sources(honuware_platform
    PRIVATE
    control_database.h
    control_database.cpp
    tenant_context.h
    tenant_header.h
    tenant_resolver.h
    tenant_resolver.cpp
    tenant_resources.h
    tenant_resources.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    control_database_test.cpp
    tenant_resolver_test.cpp
    tenant_resources_test.cpp
    tenant_physical_isolation_test.cpp
)
