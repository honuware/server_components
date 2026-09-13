@echo off
REM Start the shared development PostgreSQL server in the FOREGROUND, with its log
REM on screen. Ctrl+C stops it. Same container and same data as load_container.cmd
REM -- only the attachment differs.

docker compose up
