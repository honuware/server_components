@echo off
setlocal
REM Open a Linux shell with the honuware toolchain, this repo mounted at /src, and
REM the container joined to the Docker network your PostgreSQL is already on.
REM
REM Usage:
REM   load_container.cmd <network>     e.g. load_container.cmd knotty-net
REM   set HONUWARE_DB_NETWORK=<network> && load_container.cmd
REM
REM Find the network with:  docker network ls
REM Confirm your DB is on it, and what DNS names it answers to, with:
REM   docker network inspect <network>
REM
REM Once inside:  ./docker/build_and_test.sh
REM
REM No database environment variables are set here on purpose. The framework's
REM default Linux DB host is literally "postgresql", and a docker-compose service
REM named postgresql gets that alias on its network -- so joining the network is
REM the whole configuration. Defaults also cover port 5432 and user/password
REM docker/docker. Override with -e KNOTTYYOGA_DB_* if your setup differs.

set NETWORK=%~1
if "%NETWORK%"=="" set NETWORK=%HONUWARE_DB_NETWORK%
if "%NETWORK%"=="" (
    echo ERROR: no Docker network given.
    echo.
    echo   Usage: load_container.cmd ^<network^>
    echo.
    echo   ^<network^> is the Docker network your PostgreSQL container is on, so
    echo   this container can reach it by name. List them with: docker network ls
    exit /b 1
)

REM Resolve the repo root (this script lives in <repo>\docker\).
for %%i in ("%~dp0..") do set REPO_ROOT=%%~fi

docker image inspect honuware_build:latest >nul 2>&1
if errorlevel 1 (
    echo ERROR: image honuware_build:latest not found. Run build_container.cmd first.
    exit /b 1
)

docker network inspect %NETWORK% >nul 2>&1
if errorlevel 1 (
    echo ERROR: Docker network "%NETWORK%" does not exist.
    echo        Available networks:
    docker network ls
    exit /b 1
)

echo Repo    : %REPO_ROOT%  -^>  /src
echo Network : %NETWORK%
echo.

REM Volumes, and why:
REM   <repo>:/src            edit in Windows, build in Linux.
REM   honuware-conan2:...    persists the Conan package cache. WITHOUT THIS every
REM                          run recompiles boost/openssl from source, because
REM                          ConanCenter has few prebuilt gcc-14 binaries. Same
REM                          role as actions/cache in CI.
REM   honuware-linux-build   the build tree, kept on the CONTAINER filesystem
REM                          rather than under /src. Two reasons: Docker Desktop
REM                          bind mounts are very slow for the hundreds of small
REM                          files a C++ build produces, and it keeps the Linux
REM                          build tree from colliding with the build\ directory
REM                          Visual Studio uses. Being a named volume, it also
REM                          survives --rm, so rebuilds stay incremental.
REM                          Nuke it with: docker volume rm honuware-linux-build
REM
REM KNOTTYYOGA_DB_SSLMODE=disable: a Release build defines NDEBUG, which defaults
REM sslmode to "prefer"; the postgres image serves plaintext, so "prefer" would
REM silently fall back. Pinning it keeps a TLS misconfiguration from looking like
REM a flake. (Yes, the prefix is brand-specific -- that is what the framework code
REM currently reads; the HONUWARE_DB_* rename is a logged follow-up.)

docker run --rm -it ^
    --network %NETWORK% ^
    -v "%REPO_ROOT%:/src" ^
    -v honuware-conan2:/root/.conan2 ^
    -v honuware-linux-build:/build ^
    -e KNOTTYYOGA_DB_SSLMODE=disable ^
    -w /src ^
    honuware_build:latest bash

endlocal
