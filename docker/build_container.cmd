@echo off
REM Build the honuware Linux build/test client image.
REM
REM Run this once (and again whenever docker\Dockerfile changes). The image holds
REM only the toolchain -- the source is bind-mounted at run time, so editing code
REM does NOT require rebuilding this image.
REM
REM Usage: build_container.cmd

REM Note the "%~dp0." -- %~dp0 ends in a backslash, and "...\" would escape the
REM closing quote and mangle the argument. The trailing dot keeps it a valid path.
docker build -t honuware_build:latest -f "%~dp0Dockerfile" "%~dp0."
if errorlevel 1 (
    echo.
    echo ERROR: image build failed.
    exit /b 1
)

echo.
echo Built image honuware_build:latest
echo Next: load_container.cmd ^<docker-network-your-postgres-is-on^>
