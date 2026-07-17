# Linux build/test client for the honuware components.
#
# Purpose: build the components and run honuware_test_runner on Linux from a
# Windows dev box, against a PostgreSQL you already have running in Docker. It is
# the local twin of .github/workflows/ci.yml — same base image, same packages,
# same Conan — so "green in here" means "green in CI" means "builds in the
# consuming app's deploy image".
#
# This image contains the TOOLCHAIN ONLY. The source is bind-mounted at run time
# (see load_container.cmd) so you can edit in Windows and rebuild in here without
# rebuilding the image.
#
# Build:  docker\build_container.cmd
# Run:    docker\load_container.cmd <docker-network-your-postgres-is-on>

# Same builder image as the consuming app's package/Dockerfile. Do not bump this
# without reading the builder/runtime glibc pairing note: the app runs its
# binaries on ubuntu:22.04 (glibc 2.35) and you may not build against a newer
# glibc than you run on.
FROM gcc:14.2.0

# cmake      : the build system (bookworm ships 3.25 — see the CMP0167 guard in
#              the top-level CMakeLists for why that version matters).
# git        : a consuming app pulls honuware via FetchContent at configure time.
# python3-pip: to install Conan.
# libkrb5-dev: NOT optional. The static libpq that libpqxx pulls in transitively
#              auto-detects GSSAPI from system headers, and the top-level
#              CMakeLists appends -lgssapi_krb5 on non-Windows to resolve it.
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        cmake \
        git \
        python3-pip \
        libkrb5-dev \
    && rm -rf /var/lib/apt/lists/*

# Conan 2.x is required: the 1.x index is missing recipes we depend on
# (e.g. crowcpp-crow/1.3.2). --break-system-packages is required on bookworm
# (PEP 668).
RUN pip3 install --break-system-packages --quiet 'conan>=2.0,<3.0' \
    && conan profile detect --force

# Baked so the detected profile survives even before the Conan cache volume is
# populated; the volume mount at run time copies this content in on first use.
ENV CONAN_HOME=/root/.conan2

WORKDIR /src
CMD ["bash"]
