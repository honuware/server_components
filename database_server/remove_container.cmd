@echo off
REM Remove the shared PostgreSQL container.
REM
REM The DATA SURVIVES this -- the cluster lives in a bind mount on the host (see
REM docker-compose.yml), not inside the container, so load_container.cmd brings
REM everything back. To actually discard the databases, stop the container and
REM delete that directory.
REM
REM Note this kills the database for EVERY repo, not just the one you are in.

docker rm -f knotty-postgres-docker
