# honuware server components — dual-mode top-level build.
#
# This file works in TWO modes and detects which via PROJECT_IS_TOP_LEVEL:
#
#   STANDALONE (PROJECT_IS_TOP_LEVEL == ON): cmake is pointed at this repo
#   directly. honuware owns the full project scaffolding — the C++ standard,
#   compiler flags, the ONLY_TARGETS link policy, Conan (find_package + the
#   ${XXX_LIB} imports), the gssapi link — and builds honuware_test_runner, the
#   artifact that proves the components stand on their own with ZERO application
#   code on the include path.
#
#   CONSUMED (PROJECT_IS_TOP_LEVEL == OFF): a consuming app (e.g. knottyyoga)
#   pulls this in via add_subdirectory() / FetchContent. The APP owns project(),
#   Conan, the ${XXX_LIB} variables, the compiler/link flags, and its own test
#   executable — so all of that is skipped here. Only the component-library
#   declarations run: the layering module, the honuware_add_component targets,
#   the component subdirectories, the inter-component link edges, and the layer
#   validation. The app inherits our component targets and links its app code on
#   top.
#
# WHY project() / cmake_minimum_required() ALWAYS run (are NOT guarded):
#   PROJECT_IS_TOP_LEVEL is only made correct FOR THIS DIRECTORY by calling
#   project() HERE. Before this file's own project() call it is unreliable — unset
#   (falsy) when standalone, but inherited ON from the app's top-level project()
#   when consumed — i.e. exactly backwards for gating. Calling project() first and
#   THEN branching on PROJECT_IS_TOP_LEVEL is the standard dual-mode idiom (fmt,
#   googletest, Catch2, ...). Calling project() in a consumed subdirectory is a
#   harmless nested sub-project; the app's own top-level project() is unaffected
#   (verified: the app still sees PROJECT_IS_TOP_LEVEL==ON after we return).
cmake_minimum_required(VERSION 3.24)
project(honuware_components)

if(PROJECT_IS_TOP_LEVEL)
    # ===== STANDALONE-ONLY scaffolding ======================================
    # The consuming app owns every one of these; honuware must not set them when
    # pulled into the app's build (the app's values are inherited into our scope).
    set(CMAKE_CXX_STANDARD 20)

    # Changes how Boost is found and used. Use the old way for now (matches the app).
    #
    # The if(POLICY) guard is REQUIRED, not defensive noise: CMP0167 only exists
    # in CMake 3.30+, and cmake_policy(SET) on an unknown policy is a hard ERROR
    # ("Policy CMP0167 is not known to this version of CMake"), not a warning. We
    # declare cmake_minimum_required(VERSION 3.24) and the README promises 3.24+,
    # so an unguarded SET would break configure on every CMake in [3.24, 3.30) —
    # including the 3.25 that Debian bookworm (the gcc:14.2.0 build image, and the
    # Linux CI job) installs from apt. On <3.30 the policy simply doesn't exist and
    # FindBoost is the only lookup path anyway, so skipping the SET there yields
    # exactly the behaviour the SET asks for on 3.30+.
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 OLD)
    endif()

    if(!WIN32)
        set(CMAKE_C_COMPILER "/usr/bin/gcc")
        set(CMAKE_CXX_COMPILER "/usr/bin/g++")
        add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)
        add_compile_options(-std=c++20)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
    endif()

    if(WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20 /EHsc /MDd /Zi /Od /wd4267 /wd4244 /wd4996 /D_DEBUG /DDEBUG /DEBUG /D_WIN32_WINNT=0x0601")
        set(PDB_NAME "${LINK_FLAGS} /DEBUG /PDB")
    endif()

    set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

    # Layering enforcement: reject any target_link_libraries item that is not an
    # actual CMake target, so a bare/typo'd library name fails configuration
    # instead of silently resolving to a system library. Must be set BEFORE any
    # target is created so the per-target LINK_LIBRARIES_ONLY_TARGETS property
    # inherits it. (Consumed mode inherits the app's ON value, so enforcement
    # still applies to our targets there.)
    set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)

    # Static libpq (pulled transitively by libpqxx) auto-detects GSSAPI from
    # system headers at build time; the Conan recipe doesn't declare the system
    # dep, so we link it ourselves. CMAKE_CXX_STANDARD_LIBRARIES is appended at
    # the VERY END of the link line, after all Conan static archives — critical
    # because the GNU linker processes left-to-right and would discard
    # gssapi_krb5 symbols if it appeared before libpq.a. (Consumed mode: the app
    # supplies its own gssapi link.)
    if(NOT WIN32)
        set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lgssapi_krb5")
    endif()
endif()

# ===== ALWAYS RUN (both modes) ==============================================
# The component layer DAG + the honuware_add_component / honuware_validate_layering
# helpers that make it a build-time invariant (framework-only variant here).
#
# In consumed mode this include() runs in OUR subdirectory scope: it redefines the
# (byte-for-byte identical) helper functions globally and sets the framework-only
# HONUWARE_ALLOW_* / HONUWARE_COMPONENTS in OUR scope only. Because add_subdirectory
# scoping keeps those set()s from propagating to the parent, it cannot clobber the
# app's own app-superset DAG — the app's final honuware_validate_layering() still
# sees its own (app-inclusive) allow-lists.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/honuware_layering.cmake)

# The layered component targets (framework-only; no app targets):
#   honuware_foundation = util core (types, json, logging, http, image, ...)
#   honuware_data       = sql_util core (database_access, schema, json, procs)
#   honuware_services   = util/secrets + util/mail + the config_secrets storage
#                         table (the secrets service owns it); links data
#   honuware_square     = util/square Square API client; links foundation only
#   honuware_platform   = framework db_schema + table_helpers, business_logic
#                         auth/images/migration engine, web core; links services
#   honuware_testing    = reusable test harness (db support, matchers, endpoint
#                         test helper, service doubles); links platform + square
#   honuware_tests      = the component test-case bag (every *_test.cpp); the
#                         component CMakeLists add their tests into this target.
honuware_add_component(NAME honuware_foundation)
honuware_add_component(NAME honuware_data)
honuware_add_component(NAME honuware_services)
honuware_add_component(NAME honuware_square)
honuware_add_component(NAME honuware_platform)
honuware_add_component(NAME honuware_testing)
honuware_add_component(NAME honuware_tests)

# The shared component CMakeLists register their *_test.cpp into
# ${HONUWARE_TESTS_TARGET}. Set it BEFORE the component add_subdirectory() calls
# below. This is set in BOTH modes: honuware owns the component tests, so when an
# app consumes honuware the component tests build into honuware_tests (this repo's
# target), NOT the app's test lib — which is precisely "component tests removed
# from the app test lib". (The app stops setting HONUWARE_TESTS_TARGET so it
# inherits this value in our scope.)
set(HONUWARE_TESTS_TARGET honuware_tests)

if(PROJECT_IS_TOP_LEVEL)
    # ===== STANDALONE-ONLY: Conan =========================================
    # The consuming app runs Conan and owns the ${XXX_LIB} variables (its
    # conanfile is a superset of ours), so it includes ConanLibImports.cmake
    # itself BEFORE pulling us in; we inherit the variables from its scope.
    #
    # Note the language service providers (conan_provider.cmake) are DEFERRED and
    # won't run until something is requested, so ConanLibImports.cmake won't be
    # generated yet. A benign find_package for a piece of Boost we use forces it.
    find_package(Boost 1.83 REQUIRED COMPONENTS filesystem)

    # Find Conan2 generated libraries (${XXX_LIB} variables).
    include(ConanLibImports.cmake)
endif()

# The harness keeps the repo root on its include path for any "test/..." refs
# that don't resolve through components/testing's own root. CMAKE_CURRENT_LIST_DIR
# is honuware's repo root in both modes, so this runs unconditionally.
target_include_directories(honuware_testing PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# Each component owns its include root (set inside its own CMakeLists), so they
# are processed in dependency order (foundation -> data -> services -> square ->
# platform -> testing) with no application code anywhere on the include path.
add_subdirectory(components/foundation)
add_subdirectory(components/data)
add_subdirectory(components/services)
add_subdirectory(components/square)
add_subdirectory(components/platform)
add_subdirectory(components/testing)

# Two inter-component link edges lived APP-SIDE in knottyyoga (in src/CMakeLists.txt
# and test/CMakeLists.txt), so they were never in the shared component CMakeLists
# and did not travel with components/. Declare them here, where this repo's
# top-level owns the composition — mirroring where the app declared them:
#   platform -> services -> data -> foundation  (the core downward chain; the
#     component files only carried platform's libsodium edge, not this chain)
#   testing  -> platform + square + gtest/pqxx/crow  (the harness's own deps)
# Without these, honuware_platform / honuware_testing inherit no PUBLIC include
# dirs and every "util/...", "sql_util/...", "crow.h", "gtest/gtest.h" include
# fails to resolve. These run in BOTH modes — in consumed mode the app removed its
# own copies of these edges and relies on us to declare them.
target_link_libraries(honuware_platform PUBLIC
    honuware_services
    honuware_data
    honuware_foundation)

target_link_libraries(honuware_testing PUBLIC
    honuware_platform
    honuware_square
    ${GTEST_LIB}
    ${PQXX_LIB}
    ${CROW_LIB})

# honuware_tests (the component test-case bag) uses the harness pervasively.
target_link_libraries(honuware_tests PUBLIC honuware_testing)

# Now that every component's link edges are declared, enforce the layer DAG
# against the fully-resolved link graph. In consumed mode this validates only the
# framework components (our framework-only DAG lives in this scope); the app runs
# its own honuware_validate_layering() over its app-superset DAG afterward.
honuware_validate_layering()

if(PROJECT_IS_TOP_LEVEL)
    # ===== STANDALONE-ONLY: the component test runner =====================
    # Its own main composes a FRAMEWORK-ONLY schema and drives the shared harness
    # against Postgres. This is the artifact that proves the components stand on
    # their own. A consuming app builds its OWN test executable and does not want
    # ours, so it is skipped in consumed mode.
    add_executable(honuware_test_runner test/main.cpp)
    if(WIN32)
        set_target_properties(honuware_test_runner PROPERTIES LINK_FLAGS "/PDB:honuware_test_runner.pdb")
    endif()
    target_link_libraries(honuware_test_runner ${GTEST_LIB} honuware_testing honuware_tests)

    # Copy the CA certificate bundle to the build output for SSL verification (the
    # Square/HTTP clients read it); keeps parity with the app build.
    add_custom_command(TARGET honuware_test_runner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/certs"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/certs/cacert.pem"
            "${CMAKE_BINARY_DIR}/certs/cacert.pem"
        COMMENT "Copying CA certificate bundle to build output"
    )
endif()
