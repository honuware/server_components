# Phase 2.3 (SG5, `⇦ tenancy`): the migration list is split into two namespaced
# streams. `migration_runner` (the generic apply engine), `migrate_command` (its
# generic CLI wrapper — takes a migration vector as input, does no composition),
# `migration_namespace`, and `framework_migrations` are framework infrastructure
# and ship in honuware_platform. The app composition root (`app_migrations` +
# `all_migrations`, which concatenates framework ++ app streams) is app-side and
# lives in src/business_logic/migration/ (Phase 4.1: moved out of this component
# so components/ carries no app-named targets — see that CMakeLists).
target_sources(honuware_platform
    PRIVATE
    migration_namespace.h
    migration_namespace.cpp
    framework_migrations.h
    framework_migrations.cpp
    migrate_command.h
    migrate_command.cpp
    migration_runner.h
    migration_runner.cpp
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    migration_namespace_test.cpp
    migrate_command_test.cpp
    migration_runner_test.cpp
)
