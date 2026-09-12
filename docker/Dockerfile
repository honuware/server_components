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

# curl       : to fetch the pinned CMake tarball below.
# git        : a consuming app pulls honuware via FetchContent at configure time.
# python3-pip: to install Conan.
# libkrb5-dev: NOT optional. The static libpq that libpqxx pulls in transitively
#              auto-detects GSSAPI from system headers, and the top-level
#              CMakeLists appends -lgssapi_krb5 on non-Windows to resolve it.
#
# NOTE cmake is deliberately NOT in this list any more (Phase 12.2) — see below.
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        curl \
        ca-certificates \
        git \
        python3-pip \
        libkrb5-dev \
    && rm -rf /var/lib/apt/lists/*

# CMake from the OFFICIAL BINARY TARBALL, pinned by version AND SHA-256 (Phase 12.2).
#
# bookworm apt ships CMake 3.25, which is the whole problem: the dev boxes run
# 4.x, so every Linux gate was verifying a build nobody performs. Worse, 3.25 is
# below the 3.30 floor where CMP0167 exists, so `if(POLICY CMP0167)` was always
# FALSE here and Linux silently used FindBoost module mode while Windows used
# config mode. Raising this flips that branch on for the first time.
#
# Why a tarball rather than apt or pip: Kitware's APT repo reintroduces
# "whatever is current" instead of a fixed build, and `pip install cmake` drags a
# Python dependency into images that otherwise need none -- which, after the
# Store-alias python.exe stub broke Meson for libpq (6.2b), is exactly the kind
# of incidental dependency worth refusing. The checksum makes the image
# reproducible in a way `apt-get install cmake` never was.
#
# All five images and CI move together and pin the SAME version. A partial raise
# would mean several CMake versions across the gates, which is worse than none.
ARG CMAKE_VERSION=4.4.3
ARG CMAKE_SHA256=d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
RUN set -eux; \
    curl -fsSL -o /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz"; \
    echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum -c -; \
    mkdir -p /opt/cmake; \
    tar -xzf /tmp/cmake.tar.gz -C /opt/cmake --strip-components=1; \
    rm /tmp/cmake.tar.gz; \
    ln -sf /opt/cmake/bin/cmake /usr/local/bin/cmake; \
    ln -sf /opt/cmake/bin/ctest /usr/local/bin/ctest; \
    ln -sf /opt/cmake/bin/cpack /usr/local/bin/cpack; \
    cmake --version

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
