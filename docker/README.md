# Linux build/test client

Build the components and run the full test suite on **Linux**, from a Windows dev
box, against a **PostgreSQL you already have running in Docker**.

This is the local twin of `.github/workflows/ci.yml` — same `gcc:14.2.0` base,
same packages, same Conan settings, same CMake flags. Green here means green in
CI, which in turn means it builds in a consuming app's deploy image. Use it to
reproduce a CI result without pushing, and to catch Linux/GCC problems that a
Windows/MSVC build cannot see.

## Prerequisites

A PostgreSQL container on a Docker network, with a login role that may create
databases. Anything matching the framework defaults works with **no
configuration at all**:

| Setting | Default | Why it lands on its feet |
|---|---|---|
| host | `postgresql` | A `docker-compose` service named `postgresql` gets that DNS alias on its network — and `postgresql` is the framework's default Linux host. |
| port | `5432` | The container port, reached directly over the Docker network. Host port mappings are irrelevant here. |
| user / password | `docker` / `docker` | — |
| bootstrap database | `docker` | See the trap below. |

Check what you have:

```
docker network ls
docker network inspect <network>          # is your DB on it? what names does it answer to?
```

> **The libpq trap.** The connection that issues `CREATE DATABASE` cannot name the
> database it is about to create, so it omits the database name entirely — and
> **libpq defaults an empty database name to the *user name***. A database called
> `docker` must therefore exist. The official postgres image creates one for free
> as long as you **leave `POSTGRES_DB` unset**, because it then defaults it to
> `POSTGRES_USER`. Setting `POSTGRES_DB` removes it and startup fails with a
> baffling `database "docker" does not exist`.

## Use

```
docker\build_container.cmd                 REM once, and after Dockerfile changes
docker\load_container.cmd <network>        REM opens a Linux shell
```

Then inside the shell:

```
./docker/build_and_test.sh                        # full build + full suite
./docker/build_and_test.sh --gtest_filter=Person*  # args go to the runner
```

Measured on a 32-core box (2026-07-16):

| Run | Wall clock |
|---|---|
| Cold — empty Conan cache, 21 dependencies compiled from source | **~5 min** |
| Warm — full rebuild + all 1310 tests | **~45 s** (tests alone ~33 s) |

ConanCenter publishes **no** prebuilt binaries for gcc 14, so the first run
compiles boost, openssl, abseil, libpq and friends from source. That is heavily
parallel, so the cold number scales almost inversely with core count — expect it
to be several times longer on a smaller machine (CI runners have 2–4 cores). The
cost is paid once: the `honuware-conan2` volume keeps the package cache and
`honuware-linux-build` keeps the build tree, so later runs are incremental.

Keep the shell open while iterating; each `--rm` container is disposable but the
volumes are not.

## Sharing one PostgreSQL between Windows and Linux clients

This is the intended arrangement, and it works because **each suite owns its own
database**, resolved from the schema injected into its test main:

| Client | Database |
|---|---|
| honuware component tests (`honuware_test_runner`) | `honuware_test` |
| a consuming app's tests (e.g. knottyyoga) | its own, e.g. `test_knottyyoga` |

Windows clients reach the same server through the published host port
(`localhost:5432`, the default on Windows); Linux clients reach it by name over
the Docker network. Same server, same data directory, different databases — so a
Windows app-test run and a Linux component-test run coexist happily.

> **Do not run the *same* suite from Windows and Linux at once.** Both would own
> `honuware_test`, and each run **DROPs and CREATEs** it at startup. The loser
> gets its database deleted mid-run, or blocks on `DROP DATABASE` because the
> other still holds connections. Different suites are fine; the same suite twice
> is not.

## Troubleshooting

**`could not translate host name "postgresql"`** — the container is not on the
same network as your database, or your database's service is not named
`postgresql`. Check `docker network inspect <network>` and pass the right network
to `load_container.cmd`; if the alias differs, add
`-e HONUWARE_DB_HOST=<name>` to the `docker run` line.

**`database "docker" does not exist`** — see the libpq trap above.

**`bash: ./docker/build_and_test.sh: Permission denied`** — only bites on a real
Linux clone, not through the Windows bind mount (which reports everything as
executable). Either run it as `bash docker/build_and_test.sh`, or set the bit in
Git once with `git update-index --chmod=+x docker/build_and_test.sh`.

**`bad interpreter: No such file or directory`, or `$'\r': command not found`** —
the script got CRLF line endings from a checkout with `core.autocrlf=true`.
`.gitattributes` pins `*.sh` to LF to prevent this; re-clone or run
`git add --renormalize .`.

**A stale or confused build tree** — `docker volume rm honuware-linux-build`.

**Want to start Conan from scratch** — `docker volume rm honuware-conan2`, and
accept the 30–60 minute rebuild.

**Which DB environment variables?** The canonical names are `HONUWARE_DB_*`
(`HOST` / `PORT` / `USER` / `PASSWORD` / `NAME` / `SSLMODE` / `SSLROOTCERT`), read
by `components/data/sql_util/database_access/database_helper_init.h`. The legacy
`KNOTTYYOGA_DB_*` names are still accepted as a fallback (via
`Util::GetEnvWithFallback`), so older deploy configs keep working during the
transition.
