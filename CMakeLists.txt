# honuware server components — standalone build.
#
# This repo builds the reusable honuware component libraries and a test runner
# that exercises them against a Postgres instance. A consuming application
# (e.g. knottyyoga) does NOT use this top-level file; it pulls components/ in via
# FetchContent and supplies its own app targets on top. This file is what proves
# the components build with ZERO application code on the include path.
cmake_minimum_required(VERSION 3.24)
project(honuware_components)

set(CMAKE_CXX_STANDARD 20)

# Changes how Boost is found and used. Use the old way for now (matches the app).
cmake_policy(SET CMP0167 OLD)

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
# actual CMake target, so a bare/typo'd library name fails configuration instead
# of silently resolving to a system library. Must be set BEFORE any target is
# created so the per-target LINK_LIBRARIES_ONLY_TARGETS property inherits it.
set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)

# The component layer DAG + the honuware_add_component / honuware_validate_layering
# helpers that make it a build-time invariant (framework-only variant here).
include(${CMAKE_CURRENT_LIST_DIR}/cmake/honuware_layering.cmake)

# Static libpq (pulled transitively by libpqxx) auto-detects GSSAPI from system
# headers at build time; the Conan recipe doesn't declare the system dep, so we
# link it ourselves. CMAKE_CXX_STANDARD_LIBRARIES is appended at the VERY END of
# the link line, after all Conan static archives — critical because the GNU
# linker processes left-to-right and would discard gssapi_krb5 symbols if it
# appeared before libpq.a.
if(NOT WIN32)
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lgssapi_krb5")
endif()

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
# ${HONUWARE_TESTS_TARGET}. In this repo that is honuware_tests; a consuming app
# sets it to its own test lib. Must be set BEFORE the component add_subdirectory()
# calls below.
set(HONUWARE_TESTS_TARGET honuware_tests)

# Note the language service providers (conan_provider.cmake) are DEFERRED and
# won't run until something is requested, so ConanLibImports.cmake won't be
# generated yet. A benign find_package for a piece of Boost we use forces it.
find_package(Boost 1.83 REQUIRED COMPONENTS filesystem)

# Find Conan2 generated libraries (${XXX_LIB} variables).
include(ConanLibImports.cmake)

# The harness keeps the repo root on its include path for any "test/..." refs
# that don't resolve through components/testing's own root.
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
# fails to resolve.
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
# against the fully-resolved link graph.
honuware_validate_layering()

# The standalone test runner: its own main composes a FRAMEWORK-ONLY schema and
# drives the shared harness against Postgres. This is the artifact that proves
# the components stand on their own.
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
