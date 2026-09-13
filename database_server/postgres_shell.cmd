@echo off
REM Open a psql shell on the shared development PostgreSQL server.
REM
REM Connects to the `docker` bootstrap database; switch with \c once inside. This
REM execs into the RUNNING container rather than starting a second one -- the
REM older `docker compose run` form spun up an extra container against the same
REM data directory.
REM
REM Useful once connected:
REM
REM   \l                              list databases (see README for the inventory)
REM   \c honuware_test_windows        switch database
REM   \dt                             list tables in the current database
REM   \q                              quit
REM
REM Pass a database name to start there directly:
REM
REM   postgres_shell.cmd knottyyoga

if "%~1"=="" (
    docker exec -it knotty-postgres-docker psql -U docker -d docker
) else (
    docker exec -it knotty-postgres-docker psql -U docker -d %1
)
