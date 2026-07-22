# honuware — server components

Reusable, application-agnostic C++ building blocks for Crow-based web servers,
extracted from a production application. A new site links these components and
supplies only its own domain code (tables, endpoints, business logic) on top.

## The layer stack

Components stack in fixed layers; a target may only link targets **below** it.
This is enforced at configure time by `cmake/honuware_layering.cmake` — an
upward or sideways edge fails the build.

```
honuware_tests        (component test-case bag — every *_test.cpp)
      ▲
honuware_testing      reusable harness: Postgres test support, gtest matchers,
      │               endpoint test helper, service doubles (secrets/mail/square/http)
      ▼
honuware_platform     framework db_schema + table_helpers, business_logic
      │               (auth / images / migration engine), web core (WebApp,
      │               middleware guards, generic CRUD + admin-metadata endpoints)
      ▼
honuware_services     util/secrets + util/mail (+ the config_secrets storage table)
      ▼
honuware_data         sql_util core (database_access/Transaction, schema, json,
      │               stored procedures)
      ▼
honuware_foundation   util core (types, json_value, logging, thread_pool,
                      date_time, error handling, file util, image resize, http)
```

`honuware_square` (a generic Square API client) is a **side branch** on
`honuware_foundation` only — the platform layer deliberately may not link it;
only a consuming app's payment logic does.

## What is NOT here

Anything application- or brand-specific: domain tables (classes, products,
purchases, …), app endpoints, payment/scheduling business logic, brand strings,
and the app composition roots (`main.cpp`, database bootstrap, endpoint
registration). Those live in the consuming application. If the standalone build
here ever needs an application header, a boundary has been broken.

## Build & test

Prerequisites: a C++20 compiler (MSVC 2019+ on Windows, GCC on Linux),
CMake 3.24+, Conan 2.x, and a reachable PostgreSQL instance for the tests.

On Windows, open the folder in Visual Studio (`CMakeSettings.json` wires up the
Conan toolchain) or:

```bash
mkdir build && cd build
conan install ..
cmake ..
cmake --build .
```

On Linux, pass `--output-folder` and the generated toolchain explicitly — the
recipe's `layout()` is `vs_layout`, which is Windows-oriented:

```bash
conan install . --output-folder=build --build=missing \
    -s build_type=Release -s compiler.cppstd=17
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/build/conan/conan_toolchain.cmake"
cmake --build build -j"$(nproc)"
```

You also need `libkrb5-dev` on Linux: the static libpq that libpqxx pulls in
auto-detects GSSAPI, so the build links `-lgssapi_krb5`. Conan 2.28+ puts the
toolchain under `build/conan/`; older versions put it at `build/`.

**`.github/workflows/ci.yml` is the authoritative, always-tested version of these
steps** — it runs exactly this recipe on every push. Read it if anything here
drifts.

This produces the seven component libraries and the `honuware_test_runner`
executable. The runner composes a **framework-only** schema (via
`MakeFrameworkTables`) into a database named `honuware_test` and runs the full
component test suite against it:

```bash
cd build && ./honuware_test_runner
```

Run it from the build directory: the real HTTP client resolves its CA bundle at
the working-directory-relative `certs/cacert.pem`, which the build copies there.

The runner DROPs and CREATEs `honuware_test` itself on every run, so it needs a
PostgreSQL where the login role may create databases. Connection settings come
from `HONUWARE_DB_HOST` / `_PORT` / `_USER` / `_PASSWORD` / `_SSLMODE` (the legacy
`KNOTTYYOGA_DB_*` names are still accepted as a fallback; defaulting to host
`postgresql`, user/password `docker`/`docker` on Linux) — see
`components/data/sql_util/database_access/database_helper_init.h`. The bootstrap
connection that issues `CREATE DATABASE` omits the database name, and libpq
defaults an empty database name to the *user name*, so a database matching the
login user (`docker`) must also exist.

The test database name (`honuware_test`) is deliberately distinct so the suite
can share a Postgres instance with an application's own test database without
collision.

## Consuming these components

An application pins a specific commit and pulls the components in via CMake
FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(honuware
    GIT_REPOSITORY https://github.com/honuware/server_components.git
    GIT_TAG        <full commit SHA>)
FetchContent_MakeAvailable(honuware)
# honuware_platform, honuware_testing, ... are now ordinary targets to link.
```

For local co-development against a working tree, override with
`-DFETCHCONTENT_SOURCE_DIR_HONUWARE=/path/to/server_components`.

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
