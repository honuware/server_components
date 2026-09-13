@echo off
REM Create the bridge network that every honuware container shares.
REM
REM Docker isolates containers on their own networks by default, so without this
REM the build containers cannot reach the PostgreSQL container at all. Every repo
REM joins `knotty-net`, where the database answers to the alias `postgresql`.
REM
REM Run this ONCE per machine, before load_container.cmd. Running it again errors
REM harmlessly -- ignore "network with name knotty-net already exists".
REM
REM https://docs.docker.com/network/network-tutorial-standalone/

docker network create --driver bridge knotty-net
