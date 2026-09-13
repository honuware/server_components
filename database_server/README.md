# The shared development PostgreSQL server

One PostgreSQL container serves **every** repo that consumes honuware. knottyyoga,
communityfinder and server_components' own test suites all connect to this single
instance and keep their data in separate *databases* on it — not separate servers,
and not one container per repo.

That is why this setup lives here. It used to live in `knottyyoga/database_server/`,
where all three repos depended on a directory inside one of them; migration plan
Phase 14.2 moved it to the framework both apps already consume. The apps keep
pointer READMEs rather than copies.

## What it is

| Property | Value |
|---|---|
| compose service (network alias) | `postgresql` |
| container name | `knotty-postgres-docker` |
| image | `postgres:13.1` |
| network | `knotty-net` (external bridge) |
| host port | `5432` |
| user / password | `docker` / `docker` |
| bootstrap database | `docker` |

The alias on `knotty-net` is literally `postgresql`, which is exactly the
framework's default Linux database host — so a container that simply joins
`knotty-net` needs **no** connection configuration at all (host `postgresql`, port
`5432`, user/password `docker`/`docker`, `HONUWARE_DB_SSLMODE=disable` are all
defaults). Override any of them with the `HONUWARE_DB_*` variables; see
*Environment variables* in the repo README.

`POSTGRES_DB` is deliberately left unset in `docker-compose.yml`, which makes the
stock image create a bootstrap database named after the user — `docker`. The test
harness connects there to issue `DROP DATABASE` / `CREATE DATABASE` for its own
database; setting `POSTGRES_DB` removes it and the harness loses its connection
point.

The image is pinned at `postgres:13.1` and CI pins the same version, so the gate
and the dev box run identical server behaviour.

## Bringing it up

```
create_network.cmd              REM once per machine
load_container.cmd              REM background
load_container_interactive.cmd  REM foreground, log on screen
postgres_shell.cmd [database]   REM psql into the running container
remove_container.cmd            REM stop and remove (the data survives)
```

`create_network.cmd` is not optional and not a formality: Docker isolates
containers on their own networks by default, so without `knotty-net` the build
containers cannot reach the database at all. Running it twice errors harmlessly.

## Where the data lives

The cluster is a **bind mount on the host**, `./data` beside `docker-compose.yml`,
gitignored. Removing the container does not touch it.

Override the location with `HONUWARE_POSTGRES_DATA`:

```
set HONUWARE_POSTGRES_DATA=C:\Users\...\knottyyoga\database_server\data
```

**A machine that has been running this container from knottyyoga already has a
populated cluster in that repo**, and changing the path is not a migration —
PostgreSQL initialises a fresh empty cluster at whatever path it is handed, and
the old databases are simply not there. Two ways to carry it over:

- **Point at it.** Set `HONUWARE_POSTGRES_DATA` as above. Nothing moves, nothing
  is at risk. The compose project name is derived from this directory's name and
  is still `database_server`, so the existing container is recognised as belonging
  to this project.
- **Move it.** Stop the container first, move `knottyyoga\database_server\data`
  here, then start it.

Neither is urgent, because every database on this server is recreatable — see
below.

## What is on the server

Nothing here creates application databases. Each is created by the code that owns
it, which is why an empty server is not a problem:

| Database | Created by | Purpose |
|---|---|---|
| `docker` | the image, at first init | bootstrap; the harness connects here to DROP/CREATE |
| `knottyyoga` | `knottyyoga_database_helper --recreate_database` | dev/real data |
| `communityfinder` | `communityfinder_database_helper --recreate_database` | dev/real data |
| `honuware_test_windows` / `honuware_test_linux` | `honuware_test_runner`, at startup | this repo's suite |
| `test_knottyyoga_windows` / `test_knottyyoga_linux` | `knottyyoga_tests`, at startup | app suite |
| `test_communityfinder_windows` / `test_communityfinder_linux` | `communityfinder_tests`, at startup | app suite |
| `test_honuware_tenant_b_*`, `test_honuware_named_db_*` | the tenant-isolation tests, at startup | secondary databases |

**Test databases are platform-qualified** (Phase 10.2). The harness appends a token
derived at *compile time* — `_windows` under `_WIN32`, `_linux` otherwise — so a
Windows run and a Linux gate drive different physical databases and can run
**concurrently**. Three repos × two platforms is six test databases against one
server, all able to run at once.

Two consequences worth knowing:

- **Two Linux gates from two checkouts of the same repo still collide** — they
  compile to the same suffix. Accepted deliberately: the suffix is compile-time
  rather than an environment variable because the harness DROPs and CREATEs
  whatever name it is handed, and an externally-supplied string inside a
  destructive operation would need validation it does not have.
- **A green suite says nothing about the dev database.** The suites create their
  own; `knottyyoga` and `communityfinder` come only from `--recreate_database`.
  Thousands of passing tests are entirely compatible with that step never having
  been run — this cost real time once already.

`tenant_scratch` is currently on the server and is referenced by no source file in
any of the three repos. It appears to be a leftover from the tenant-isolation work
that predates the `test_honuware_tenant_b_*` naming; drop it by hand once and it
will not come back.

## Resetting a database

Use the application's own helper, never a file-level wipe:

```
set HONUWARE_ALLOW_DESTRUCTIVE=1
out\build\x64-Debug\src\database_helper\<app>_database_helper.exe --recreate_database
```

The schema is defined in code (`db_schema` plus the migration engine), so the
helper is the only thing that produces a correct database. knottyyoga's old
`schema.sql` and `clear-database.cmd` were both bootstrap-era artefacts and did
**not** move here: the first is a one-app table set that only ever ran against an
empty data directory, and the second just deleted `data\`. knottyyoga's CLAUDE.md
had already documented both as unused.

## A note on the harness paths

The per-repo *build* containers are separate from this database container and stay
in their own repos. Their directories were inconsistent — knottyyoga used
`server/docker_project/`, communityfinder used `server/docker/` — which made every
generic instruction wrong for one of the two. They are now both `server/docker/`.
This directory is the only shared piece.
