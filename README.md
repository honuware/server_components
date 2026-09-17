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

## Developer machine setup

From a bare Windows machine to a green build. Every step here has been walked
end to end; where a step exists only because something bit us, the reason is
stated rather than left as folklore.

### 1. Prerequisites

| tool | version | notes |
|---|---|---|
| Git for Windows | any current | **Re-check after a Visual Studio reinstall** — see *Known setup failures* |
| CMake | **4.4.3** | <https://cmake.org/download/> — install the exact version, not "latest" |
| Conan | **2.31.2** | <https://conan.io/downloads> — 2.x is required; the 1.x index is missing recipes used here |
| Python | 3.13 | `winget install Python.Python.3.13 --scope machine` from an **elevated** prompt |
| Visual Studio 2026 | Community or higher | with the **Desktop development with C++** workload |
| Docker Desktop | any current | needs virtualization enabled in BIOS *and* as a Windows optional feature |

**Python is not optional, and it is not obvious why.** Nothing in this repo is
written in Python. It is required because `libpq` builds with **Meson**, which is
a Python application — so a missing or broken Python breaks a C++ dependency with
an error that never mentions Python. Install it machine-scoped from an elevated
prompt as shown, **not** from the Microsoft Store: the Store installs an "app
execution alias" stub at `python.exe` that exits silently, which Meson then fails
on in a way that points nowhere near the cause.

> Version pins above are deliberate. If you change one, change it here too —
> a document that names a version but links to a download landing page serving
> "whatever is current" drifts silently and is worse than one that says nothing.

### 2. Clone

```
cd C:\Users\%USERNAME%\source\repos
git clone https://github.com/honuware/server_components.git
```

The consuming applications are separate repositories and pull honuware in via
FetchContent at a pinned SHA — you do **not** need them to build or test this one.

### 3. Configure Visual Studio

**Tools → Options → CMake → "Prefer using CMake Presets…" → *use CMake Presets if
available*.**

This is a setup step, not troubleshooting. Setting it explicitly makes the machine
deterministic instead of dependent on which files happen to exist in the tree when
VS opens. These repos are driven by `CMakePresets.json`; the old `CMakeSettings.json`
is gone from all three, so the opposite setting (*Never*) leaves VS with no
configuration at all — it stops invoking CMake and *Delete Cache and Reconfigure*
greys out, which reads like a preset bug rather than a settings one.

**Open the folder containing `CMakeLists.txt`** — for this repo, the repo root. In
the application repos it is `server\<app>_server`, **not** the repo root, and the
Angular front end is opened as a separate instance. Both apps briefly had two
candidate workspaces; opening the wrong one gives you a second `.vs` folder whose
launch entries silently do not work in the other.

### 4. First configure and build

Open the folder in Visual Studio and let CMake generation finish, or from a
developer prompt:

```
cmake --preset x64-Debug
cmake --build --preset x64-Debug
```

Conan runs automatically during configure (`CMAKE_PROJECT_TOP_LEVEL_INCLUDES`
points at `conan_provider.cmake`) and resolves against the committed `conan.lock`.
The first build compiles every dependency from source and takes a long time;
later builds reuse the Conan cache.

### 5. Generate the debug launch configuration

**A fresh clone has no `.vs` folder, and the Visual Studio command that would
normally create one is broken in VS 2026** (it writes the file but never opens it;
*Targets View → Add Debug Configuration* does nothing at all). So generate it:

```
.\tools\sync_launch_targets.ps1 -RepoPath .
copy tools\launch_defaults.example.json tools\launch_defaults.local.json
```

Then edit `launch_defaults.local.json` with your local values and re-run the
script (it stamps the file's `args`/`env` onto every generated entry). It is
gitignored because it holds credentials — the database login, and the two
**seed-time passwords** step 7 explains: `HONUWARE_MAIL_APP_PASSWORD` (a Gmail
app password, database-helper target only) and `SCHEDULER_SERVICE_ACCOUNT_PASSWORD`
(the example already carries a dev value in `all`). Without this step every
debug target launches bare — no database environment, no `--recreate_database`,
no `HONUWARE_ALLOW_DESTRUCTIVE` — and the failures look like application bugs
rather than missing configuration.

### 6. Docker, the network, and PostgreSQL

Confirm Docker works (`docker --version`), then create the shared bridge network
every container and suite uses:

```
cd database_server
create_network.cmd
```

Each container is otherwise on its own private network; `knotty-net` is what lets
the build containers reach PostgreSQL. Then start the database:

```
load_container.cmd
```

That runs a stock `postgres:13.1` image via `docker-compose.yml` as container
`knotty-postgres-docker`, publishing **5432:5432**, with user and password both
`docker`. `load_container_interactive.cmd` runs it in the foreground instead,
which is usually what you want while developing. `postgres_shell.cmd [database]`
opens a psql shell in the running container.

**One container serves all three repos** — knottyyoga, communityfinder and this
one keep their data in separate *databases* on this single server. It lives here
rather than in an application because all three depend on it; the apps carry
pointer READMEs. See `database_server/README.md` for the database inventory and
for where the cluster is stored on disk.

### 7. Create and seed the dev database

**This step is easy to miss and nothing else tells you it was missed** — the test
suites create their own databases and pass perfectly while the dev database does
not exist.

From the application repo (this framework repo has no dev database of its own):

```
set HONUWARE_ALLOW_DESTRUCTIVE=1
set HONUWARE_MAIL_APP_PASSWORD=<gmail app password>
set SCHEDULER_SERVICE_ACCOUNT_PASSWORD=<any non-empty value for dev>
out\build\x64-Debug\src\database_helper\<app>_database_helper.exe --recreate_database
```

`HONUWARE_ALLOW_DESTRUCTIVE` must be exactly `"1"` — anything else, `"true"`
included, refuses the operation. The two password variables are read **at seed
time**, by both applications, and they fail differently:

- Without `SCHEDULER_SERVICE_ACCOUNT_PASSWORD` the seed **throws** and tells you so.
- Without `HONUWARE_MAIL_APP_PASSWORD` the seed **completes silently** with an
  empty `config_secrets.mail_app_password` row. Nothing complains until the
  server first tries to send mail — a registration verification, a booking
  confirmation — and then it refuses with
  `config_secrets.mail_app_password is empty - cannot send mail`. If you see that
  message, this is the step that was skipped.

Pressing F5 on the `database_helper` debug target does the same thing, because
step 5 puts both the flag and the variables on that target. The command line is
written out anyway: the F5 route only works once the launch configuration exists,
and it hides what is actually being run.

#### Getting a Gmail app password

The framework ships **no** mail password (it is a credential, and this is a public
repo — see `components/services/util/secrets/CLAUDE.md` for the incident that
rule comes from). Each developer supplies one:

1. The password must belong to the **sender mailbox** — whatever the app's
   `kMailSenderAddress` is (`knottyyogaandspa@gmail.com` for both apps today).
   mailio logs in to SMTP using the sender address as the username, so a
   password for any other account fails as `Mail sender rejection`, which reads
   like an address problem rather than a credential one.
2. Signed in to that Google account, with 2-Step Verification on, go to
   https://myaccount.google.com/apppasswords and create one. Name it so it can
   be revoked on its own later (`knottyyoga-dev-<machine>`).
3. Google shows it as four groups — `xxxx xxxx xxxx xxxx`. The spaces are
   display only; the credential is the 16 characters. Strip them.
4. Put it in `launch_defaults.local.json` on the database-helper target (the only
   process that reads it), re-run `sync_launch_targets.ps1`, and re-run
   `--recreate_database`. Keep it out of anything committed and out of plain
   text files under `Documents`; a password manager is the right home.

Only `--recreate_database` and `--create_tenant` read the variable. `--migrate`
never touches `config_secrets`, so on an **existing** database the value is set
through the application's own tooling instead (knottyyoga:
`knottyyoga_test_helper --command=set_secret --key=mail_app_password --value=…`).

### 8. Know which database you are looking at

Each application has **two** databases and creating one does not create the other:

| database | driven by | created by |
|---|---|---|
| `<app>` | the server and the helpers | `--recreate_database`, step 7 |
| `test_<app>_windows` / `test_<app>_linux` | the test suites | dropped and recreated automatically at suite startup |

Test databases are platform-qualified so a Windows run and a Linux gate can run
concurrently. **A green suite says nothing about the dev database** — thousands of
passing tests are entirely compatible with step 7 never having been run.

### 9. Run the tests

Windows, from the build directory (the real HTTP client resolves its CA bundle at
the working-directory-relative `certs/cacert.pem`, which the build copies there):

```
cd out\build\x64-Debug
honuware_test_runner.exe
```

Linux, in the container gate:

```
docker\build_container.cmd
docker\load_container.cmd knotty-net
./docker/build_and_test.sh
```

Both platforms are gates, and neither substitutes for the other. Linux catches
what Windows cannot — CMP0167 policy behaviour, `find_package` case sensitivity,
libpq's `dbname=` handling, `-O2` dead-stripping of endpoint anchors. Windows
catches what Linux cannot — `winnt.h` macro collisions, `_putenv_s` removing a
variable when set to empty, and dangling references MSVC happens to tolerate.

### Known setup failures

Four machine-level problems cost real time during the VS2026 migration. None is
discoverable from its error message, and a new machine will hit them identically.

**The Store-alias `python.exe` stub.** Installing Python from the Microsoft Store
(or leaving the default App Execution Alias enabled) leaves a `python.exe` that
exits silently. Meson then fails while building `libpq`, and the error mentions
neither Python nor the alias. Install machine-scoped via `winget` as in step 1,
and disable the aliases under *Settings → Apps → App execution aliases*.

**Git for Windows missing after a Visual Studio reinstall.** CMake configure pulls
honuware via FetchContent and needs `git` on `PATH`. A VS reinstall can leave a
`git` that works inside the IDE but not in a plain developer prompt. Check with
`git --version` from the same shell you build in.

**Building from a plain shell instead of a developer prompt.** `cmake --build`
succeeds at configure and then fails compiling *every* file with

```
fatal error C1083: Cannot open include file: 'algorithm': No such file or directory
```

— `string_view`, `sstream`, and the rest of the C++ standard library too. Nothing
is wrong with the code or the toolchain: Ninja invokes `cl.exe` directly, and
`cl.exe` finds the standard headers through the `INCLUDE` environment variable,
which only a developer prompt sets. The error names a standard header, so it reads
like a broken compiler installation rather than a missing environment. Build from
the *Developer Command Prompt / Developer PowerShell for VS 2026*, or prefix the
build in a plain shell. From PowerShell, the outer single quotes stop PowerShell
consuming the inner ones, which `cmd` needs around the space in *Program Files*:

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build --preset x64-Debug'
```

Building from inside Visual Studio never hits this — the IDE supplies the
environment — which is what makes it surprising the first time a build is scripted.

**The unseeded dev database.** See steps 7 and 8. This is the one that looks like
an application bug: the server starts, the suites pass, and every request touching
data fails.

## Build & test

Prerequisites: a C++20 compiler (MSVC 2019+ on Windows, GCC on Linux),
CMake 3.24+, Conan 2.x, and a reachable PostgreSQL instance for the tests.

On Windows, open the folder in Visual Studio (`CMakePresets.json` defines the
`x64-Debug` configure/build/test presets and wires up the Conan toolchain via
`CMAKE_PROJECT_TOP_LEVEL_INCLUDES=conan_provider.cmake`), or drive the same
presets from a developer prompt:

```bash
cmake --preset x64-Debug
cmake --build --preset x64-Debug
ctest --preset x64-Debug
```

or configure by hand:

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

### Environment variables

Enumerated from the source, not from memory. Every `HONUWARE_*` name below is
read through `Util::GetEnvWithFallback` (`components/foundation/util/env.h`),
which tries the `HONUWARE_*` name first and falls back to the legacy
`KNOTTYYOGA_*` name — so old deploy environments keep working. A `HONUWARE_*`
that is set but *empty* still wins over the legacy name.

**Database connection** (`database_helper_init.h`). For local work you normally
need none of these — the defaults already resolve to the shared docker Postgres:

| variable | default |
|---|---|
| `HONUWARE_DB_HOST` | `localhost` on Windows, `postgresql` on Linux |
| `HONUWARE_DB_PORT` | `5432` |
| `HONUWARE_DB_USER` | `docker` |
| `HONUWARE_DB_PASSWORD` | `docker` |
| `HONUWARE_DB_NAME` | **no framework default** — the application supplies it; setting this points a deploy at an alternate database |
| `HONUWARE_DB_SSLMODE` | `prefer` in release, unset in debug; the docker gate sets `disable` |
| `HONUWARE_DB_SSLROOTCERT` | unset; required for `sslmode=verify-full` against RDS |

**The ones you actually set by hand:**

| variable | why |
|---|---|
| `HONUWARE_ALLOW_DESTRUCTIVE` | must be **exactly `"1"`** — anything else, including `"true"` or `"yes"`, blocks. Gates `--recreate_database`, which refuses rather than self-healing without it |
| `HONUWARE_SECRET_KEY` | at-rest key for `config_secrets`; non-prod falls back to a fixed dev key, so it is optional locally and **mandatory in production** |
| `HONUWARE_MAIL_APP_PASSWORD` | a **Gmail app password for the sender mailbox**, read **at seed time** (`--recreate_database` / `--create_tenant`, never `--migrate`) by the app's `create_database.cpp`, which UPDATEs the `config_secrets` row. Not needed at runtime once seeded. Unset ⇒ the seed succeeds with an empty row and the server later refuses with `config_secrets.mail_app_password is empty - cannot send mail`. How to get one: setup step 7 |
| `SCHEDULER_SERVICE_ACCOUNT_PASSWORD` | the scheduler service account. Seeding **throws** if unset; the scheduler also falls back to it when `--service_account_password` is empty |
| `PORT` | server listen port — defaults differ per application, so check the app's `main.cpp` |

**Set rarely, but real:**

| variable | behaviour |
|---|---|
| `HONUWARE_VERSION` | build version in the health response, re-read on every call; falls back to `"unknown"`. Set on the host to pin which artifact is live |
| `HONUWARE_TENANT_MODE` | `Fixed` (default — one tenant, no control database, no site header) or `Control` (multiplexes many sites off the control database's `tenants` table) |
| `HONUWARE_FIXED_SITE_KEY` | Fixed mode only; overrides the site key, which otherwise defaults to the app database name |
| `HONUWARE_CONTROL_DB_NAME` | Control mode only |
| `HONUWARE_LOG_DEST` | where the `LogXxx()` streams write |
| `HONUWARE_TRUST_PROXY`, `HONUWARE_ORIGIN_SECRET`, `HONUWARE_DEV_CORS_ORIGIN` | auth and CORS |
| `HONUWARE_APP_NAME` | theme-bundle export metadata only; empty when unset |
| `CURL_CA_BUNDLE` | overrides the working-directory-relative `certs/cacert.pem` |

**Not environment variables, despite the naming.** `HONUWARE_API_BASE`,
`HONUWARE_CRUD_ACCESS` and `HONUWARE_MOCK_OPTIONS` are **Angular
dependency-injection tokens** in the UI, configured through `environment.ts`;
setting them in the environment does nothing. `HONUWARE_SRC_DIR` is
container-side only — on Windows the equivalent is the CMake cache variable
`FETCHCONTENT_SOURCE_DIR_HONUWARE`.

### Debug targets in Visual Studio (`tools/sync_launch_targets.ps1`)

Visual Studio stores per-target debug settings — command-line `args` and `env` —
in `.vs/launch.vs.json`. Two things make that file awkward to maintain by hand:

- **`.vs/` is gitignored**, so it is disposable. Everything you type into it is
  lost the moment the folder is cleared, and clearing it is a standard fix for
  VS getting confused about a configuration.
- **VS 2026's *Debug > Debug and Launch Settings* is broken.** It writes the
  entry but never opens the file, and *Targets View > Add Debug Configuration*
  does nothing at all. Both fail with `ServiceUnavailableException: The
  VsTextManagerClass service is unavailable` in `ActivityLog.xml`. The only
  working entry point is right-clicking the root `CMakeLists.txt` >
  *Add Debug Configuration*, which cannot populate `projectTarget` and appends a
  blank duplicate every time it runs.

So generate the file instead. From a configured build tree:

```powershell
.\tools\sync_launch_targets.ps1 -RepoPath . [-Config x64-Debug] [-Defaults <file>] [-WhatIf]
```

It enumerates every executable target from the CMake file API, preserves entries
already present, prunes the blank-`projectTarget` leftovers, and skips imported
targets. `-WhatIf` prints the result without writing. `-Config` defaults to the
most recently modified directory under `out/build`, so pass it explicitly when
more than one configuration exists.

To keep `args`/`env` across `.vs/` wipes, copy `tools/launch_defaults.example.json`
to `tools/launch_defaults.local.json` (gitignored — it holds DB credentials) and
fill in your values. The script stamps them onto every generated entry: `all`
applies to all targets, and `targets` keys match the `projectTarget` label
exactly or as a wildcard, so `"*tests.exe*"` catches the nested
`name.exe (test\name.exe)` form without retyping it. This is the practical place
to put the `HONUWARE_DB_*` variables above, `SCHEDULER_SERVICE_ACCOUNT_PASSWORD`
(in `all`, so the database helper and the scheduler helper cannot disagree),
`HONUWARE_MAIL_APP_PASSWORD` (on the `*database_helper.exe*` target only — it is
a real credential, and only the seed reads it), and a `--gtest_filter` for the
test executable. Use `--gtest_filter=*` as the standing value — that runs everything,
and it is the pattern you narrow to something like `--gtest_filter=Foo.*` while
chasing a failure. An *empty* `--gtest_filter=` runs no tests at all.

The script is application-agnostic — it takes a repo path and reads only the
CMake file API — so the consuming application repos use this same copy.

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

## New-consumer checklist

Standing up a new application on these components (distilled from the two
consumers built so far). Each step names the seam it plugs into:

1. **Pin + link.** FetchContent-pin a full SHA (above). Link `honuware_platform`
   for the framework; add `honuware_square` only in your payment code (it is a
   side branch — the platform may not link it), `honuware_scheduler` if you run
   scheduled jobs, and `honuware_testing` / compose `honuware_tests` into your
   test exe.
2. **Compose the schema.** Build one `DbSchema::DatabaseInfo` = the framework
   tables **plus** your app tables. The framework never calls your
   `MakeDatabaseInfo`; your app composition root owns it.
3. **Bootstrap the database.** In your `create_database`, call
   `CreateFrameworkTables(transaction, databaseInfo)` then
   `PopulateFrameworkTables(transaction, databaseHelper, databaseInfo)` (the
   framework half — 32 tables + their indexes, and the framework seed: base
   Administrator/User roles, `admin_portal`/`staff_access` permissions + grants,
   framework `config_secrets` defaults, `permission_implications` allow-list,
   `people` photo support, security redactions, and the framework tables' admin
   metadata). Then create + seed **only your app tables**. Because the framework
   seeds its roles/permissions **first**, their ids sit ahead of yours — so
   reference framework (and your own) roles/permissions **by name**, never by a
   hard-coded id. Framework enum metadata (`admin_enums` / `admin_column_enums`)
   is *not* seeded for you; seed it app-side if your framework tables expose
   admin enum columns. Provision a scheduler service account app-side via
   `Auth::EnsureSchedulerServiceAccount` if you run the scheduler.
4. **Register + anchor endpoints.** Call `RegisterFrameworkEndpoints()` for the
   generic auth / account / photo / CRUD / health routes, then register your app
   endpoints. Endpoints self-register at file scope, so **anchor** each app
   endpoint translation unit through the volatile-anchor pattern — a plain unused
   pointer dead-strips at `-O2` and every route 404s in Release.
5. **Wire the framework seams.** Per-`WebApp` via `WebApp::SetService<T>()`:
   `PostRegisterHook` (side effects after `/api/register`) and
   `PublicPhotoTables` (the anonymous-read allow-list for `get_scaled_photo`).
   Unregistered ⇒ safe default (no hook / nothing public).
6. **Secrets.** Framework secret defaults come from `Secrets::Values`; layer your
   app/brand defaults on top. At-rest `config_secrets` need `HONUWARE_SECRET_KEY`
   (non-prod falls back to a fixed dev key).
7. **Tests.** Your test `main` calls `GlobalDatabaseTestSupport::Initialize(yourDatabaseInfo)`
   with **your own** test-database name, plus `RegisterAllEndpoints()` (anchors
   your app endpoint TUs) and your `RegisterAppSecretDefaults`. Note the harness
   builds the schema from the `DatabaseInfo` but does **not** run your
   `create_database` seed — gate the seed itself with a live `--recreate_database`.
8. **Co-dev loop.** Mirror `docker/` here + the app's docker client; build against
   a local honuware tree with `-DFETCHCONTENT_SOURCE_DIR_HONUWARE`, gate both
   suites with a test-count floor, then re-pin the SHA.

## License

Apache-2.0. See `LICENSE` and `NOTICE`.
