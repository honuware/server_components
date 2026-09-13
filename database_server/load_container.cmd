@echo off
REM Start the shared development PostgreSQL server in the background.
REM
REM Requires knotty-net to exist -- run create_network.cmd first if this fails
REM with "network knotty-net declared as external, but could not be found".
REM
REM Use load_container_interactive.cmd instead to run it in the foreground with
REM the server log on screen, which is usually what you want while developing.

docker compose up -d
