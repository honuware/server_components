# CLAUDE.md — honuware server components

Guidance for Claude Code when working in this repository. These are the reusable,
application-agnostic components extracted from a Crow/C++ web server. The #1 rule:
**nothing here may depend on an application.** No app tables, app endpoints, brand
strings, or app composition roots. If you find yourself needing one, the boundary
is wrong — stop and reconsider.

## The layer DAG is law

Components stack low→high: `foundation → data → services → platform → testing →
tests`, with `square` a side branch on `foundation` only. A target may link only
targets **below** it. This is enforced at configure time by
`cmake/honuware_layering.cmake`; an upward/sideways edge fails the build with a
"honuware layering violation" message. To add a component or change an edge, edit
the allow-lists in that file deliberately — it is the single source of truth for
the architecture.

Layer intent:
- **foundation** — `util/` core (types, json_value, logging, thread_pool,
  date_time, error handling, file util, image resize, `http/`). Includes nothing
  outside `util/`.
- **data** — `sql_util/` core (`database_access` incl. `Transaction`, `schema`,
  `json`, `stored_procedures`).
- **services** — `util/secrets` + `util/mail`, plus the `config_secrets` storage
  table the secrets service owns. Services sit **above** data, so `util/mail` and
  `util/secrets` legitimately include `sql_util/database_access` — that is a
  downward edge, not a back-edge; do not "fix" it.
- **platform** — framework `db_schema` + `table_helpers`, `business_logic`
  (auth / images / migration engine), and the web core (WebApp, middleware
  guards, generic CRUD + admin-metadata endpoints).
- **testing** — the reusable harness (Postgres test support, gtest matchers,
  endpoint test helper, service doubles). Application-agnostic: it takes the
  composed schema as an injected input rather than calling any app root.
- **tests** — the component test-case bag; each component's `*_test.cpp` is
  registered here via `${HONUWARE_TESTS_TARGET}`.

## CMake conventions

- **Register component test sources into `${HONUWARE_TESTS_TARGET}`**, never a
  hardcoded target name. The top-level `CMakeLists.txt` sets it to
  `honuware_tests`; a consuming application sets it to its own test lib. This is
  what lets the same component `CMakeLists.txt` build in both repos.
- **Always list header files in `target_sources()`** alongside their `.cpp`, and
  place each header immediately before its implementation.
- Each component owns its PUBLIC include root (set in the layer's top-level
  `CMakeLists.txt`), which establishes the `#include` prefixes (`util/...`,
  `sql_util/...`, `business_logic/...`, `test/src/util/...`). Includes use those
  full prefixes, not bare same-directory names.

## Testing conventions

- **No test fixtures.** Each test is self-contained; set up dependencies at the
  top of the test function. Do not use `class XxxTest : public ::testing::Test`.
- **All tables are pre-created** by the harness at startup
  (`GlobalDatabaseTestSupport`). Tests do NOT create tables or stored procedures;
  each test runs in a transaction that is aborted at the end.
- **Add tests when you add methods.** A new method on a class that has a
  `*_test.cpp` must get a corresponding test in the same change.
- **`ThreadPool::Shutdown()` before the next DB read.** Endpoints that enqueue
  async writes via `ThreadPool::GetInstance().Queue(...)` re-enter the test's
  transaction provider on the same libpqxx connection, which is not thread-safe.
  Call `Shutdown()` immediately after `handle_full()` returns, before the first
  DB-touching assertion.
- **Don't assume collection order.** Find items by a unique identifier, not by
  array index.

## Windows / Crow gotcha

Use the **PascalCase** `crow::HTTPMethod` aliases — `Post`, `Put`, `Patch`,
`Delete`, `Get`, `Head`, `Options` — never the SCREAMING_CASE forms. On Windows
`winnt.h` `#define DELETE`, and Crow responds by omitting the entire SCREAMING_CASE
half of the enum (so `POST`/`GET`/etc. all vanish too) whenever `<windows.h>` has
been pulled in — which libpqxx often does transitively. The PascalCase aliases are
defined unconditionally on every platform.

## Common C++ API pitfalls

- `Json::Value` (`util/json_value.h`) wraps crow::json with a custom API. Get
  values with `value.Get<int64_t>()` / `Get<std::string>()` / `Get<bool>()`,
  arrays with `value.GetArray()` / `.GetArray().size()`. There is no
  `GetInt64()`, `ArraySize()`, or `Json::Value::Object(...)`. Build objects with
  `Json::Value(Json::JsonObject{{"k", v}})`.
- `ErrorResponse` (`util/error_response.h`): `BadRequest` (400),
  `NotAuthenticated` (401), `NotAuthorized` (403), `NotFound` (404),
  `ValidationError` (400), `InternalError` (500). There is no `ServerError`.
- Always read the actual header before using an API; do not assume standard
  naming.

## String / email formatting

- Use `FormatString` with `{placeholder}` template constants (`util/types.h`) for
  dynamic strings, not line-by-line stream building.
- Wrap generated email HTML bodies with `NormalizeCrLf(...)` — mailio requires
  `\r\n` line endings and rejects the bare `\n` in raw string literals.
- Resolve a mail From-address via `::Mail::LoadSenderAddress(secrets, txn)` (note
  the leading `::Mail::` — nested namespaces can shadow it). The brand values it
  reads are supplied by the consuming application's secret defaults, not here.

## Naming

Full descriptive names, not abbreviations. `snake_case` files, `camelCase`
variables, `PascalCase` classes/functions. File names match the primary class
they contain.
