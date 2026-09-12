#!/usr/bin/env bash
#
# Build the honuware components and run the full component test suite. Intended to
# be run INSIDE the container from docker/load_container.cmd, but it is just a
# normal Linux build and works on any Linux box with the toolchain present.
#
#   ./docker/build_and_test.sh                       # full build + full suite
#   ./docker/build_and_test.sh --gtest_filter=Person*  # args go to the runner
#
# .github/workflows/ci.yml is the AUTHORITATIVE version of these steps -- it is
# what actually gates the repo. This mirrors it so you can reproduce a CI result
# locally without pushing. If the two ever disagree, CI wins.
#
# Overridable:
#   SRC_DIR             source tree            (default /src)
#   BUILD_DIR           build tree             (default /build)
#   MIN_EXPECTED_TESTS  floor for the count assertion (default 1700)
#
# THE FLOOR TRACKS THE COUNT AND MUST BE RAISED AS THE SUITE GROWS (Phase 12.1).
# It exists to catch a suite that silently stops running -- the endpoint-anchor
# dead-strip found in 6.2 linked fine and ran a FRACTION of the tests. A floor
# left at its original value stops being able to detect that: 1000 against an
# actual 1772 would have let 43% of the suite vanish unnoticed. All three repos
# had this same defect, each set once and never revisited.
#
# Actual count 2026-09-11: 1772 (Windows and Linux agree).

set -euo pipefail

# Anything unrecognised is forwarded to the test runner, so --help must be caught
# explicitly -- otherwise asking for help silently starts a ~45-minute build.
case "${1:-}" in
    -h|--help)
        # Print the header comment block: skip the shebang, strip the leading
        # "# ", and stop at the first line that is not a comment. Driven off the
        # block's actual shape so it cannot drift out of sync with an edit.
        awk 'NR == 1 { next }
             /^#/    { sub(/^# ?/, ""); print; next }
                     { exit }' "$0"
        exit 0
        ;;
esac

SRC_DIR=${SRC_DIR:-/src}
BUILD_DIR=${BUILD_DIR:-/build}
MIN_EXPECTED_TESTS=${MIN_EXPECTED_TESTS:-1700}

echo "[honuware] source : $SRC_DIR"
echo "[honuware] build  : $BUILD_DIR"

# --- Conan ------------------------------------------------------------------
# Same settings as the app's package/build_linux_release.sh. --output-folder
# side-steps the recipe's Windows-oriented vs_layout. cppstd=17 governs only the
# DEPENDENCY builds; the components themselves compile as C++20 via
# CMAKE_CXX_STANDARD -- the same combination the app ships with.
#
# Note: conanfile.py's init() writes ConanLibImports.cmake next to the RECIPE
# (the repo root, i.e. into your mounted Windows tree), not into --output-folder.
# That is expected and harmless: its contents are derived purely from the
# `libraries` list and are platform-independent, so it is byte-identical to the
# one Visual Studio generates. It is gitignored.
#
# CMakeUserPresets.json is the OTHER file conan writes next to the recipe, and
# unlike ConanLibImports.cmake it is NOT harmless here. Visual Studio switches to
# CMake Presets mode the moment it sees one and then ignores CMakeSettings.json --
# where this project's CONAN_CMD / CMAKE_PROJECT_TOP_LEVEL_INCLUDES wiring lives --
# leaving VS with no launchable configuration and a Debug menu that silently does
# nothing. Since /src is a bind mount of the Windows tree, a Linux gate run would
# otherwise break the developer's IDE from inside the container, writing an
# `include` that points at the Linux /build path. Verified: without this flag the
# file appears in the mounted tree; with it, it does not.
echo "[honuware] conan install ..."
# --lockfile is what makes conan.lock load-bearing (Phase 12.3). A lockfile that
# is merely committed does NOTHING -- the same trap as the committed Windows
# profile in Phase 2, which had no effect until it was named in
# CONAN_HOST_PROFILE. Without this flag Conan re-resolves, and the whole point is
# that it must not: identical conanfiles previously resolved libpq 15.5 on one
# machine and 17.11 on another, and those build through entirely different build
# systems (MSBuild vs Meson -- 6.2b).
#
# ONE lockfile covers BOTH platforms: verified 2026-09-11, Windows and Linux
# resolve an identical version for every package, so a per-platform lockfile
# would be two copies of the same answer.
conan install "$SRC_DIR" \
    --output-folder="$BUILD_DIR" \
    --lockfile="$SRC_DIR/conan.lock" \
    --build=missing \
    -s build_type=Release \
    -s compiler.cppstd=17 \
    -c tools.cmake.cmaketoolchain:user_presets=''

# Conan 2.28+ nests generators under conan/; older versions put the toolchain at
# the output-folder root. Probe rather than assume.
TOOLCHAIN=""
for candidate in \
    "$BUILD_DIR/conan_toolchain.cmake" \
    "$BUILD_DIR/conan/conan_toolchain.cmake" \
    "$BUILD_DIR/build/conan_toolchain.cmake"; do
    if [ -f "$candidate" ]; then
        TOOLCHAIN="$candidate"
        break
    fi
done
if [ -z "$TOOLCHAIN" ]; then
    echo "ERROR: conan_toolchain.cmake not found under $BUILD_DIR" >&2
    find "$BUILD_DIR" -name 'conan_toolchain.cmake' >&2 || true
    exit 1
fi
echo "[honuware] toolchain: $TOOLCHAIN"

# --- Configure + build ------------------------------------------------------
echo "[honuware] cmake configure ..."
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

echo "[honuware] cmake build ..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- Test -------------------------------------------------------------------
# Run from the build dir: the build copies certs/cacert.pem to
# ${CMAKE_BINARY_DIR}/certs and the real HTTP client resolves it at the
# CWD-relative "certs/cacert.pem". (The fixture-reading tests use __FILE__ and are
# CWD-independent.)
#
# The runner DROPs and CREATEs its own test database, so nothing needs seeding.
#
# Phase 10.2: that database is now PLATFORM-QUALIFIED -- this Linux gate drives
# `honuware_test_linux` while a Windows run drives `honuware_test_windows`, from
# the same base name suffixed at compile time. The two no longer collide, so the
# old "do not run this at the same time as a Windows honuware_test_runner"
# warning no longer applies and has been removed.
#
# Still true: two Linux gates from two checkouts of THIS repo would collide, since
# they compile to the same suffix. That trade was accepted deliberately -- see
# global_database_test_support.h for why the suffix is compile-time rather than an
# environment variable.
#
# The old unsuffixed `honuware_test` database is now orphaned. It is scratch data
# that gets dropped and recreated anyway, so it can simply be dropped by hand once.
cd "$BUILD_DIR"

echo "[honuware] running component tests ..."
./honuware_test_runner "$@" 2>&1 | tee test-output.txt

# The count assertion is load-bearing -- see the long note in
# .github/workflows/ci.yml. Short version: honuware_tests is a STATIC library of
# self-registering gtest TEST()s that main.cpp references no symbol from, so a
# linker may drop every test object, leaving RUN_ALL_TESTS() to find 0 tests and
# exit 0 -- a green run that verified nothing.
#
# Only enforced on an unfiltered run: a --gtest_filter run is legitimately small.
if [ "$#" -ne 0 ]; then
    echo "[honuware] filtered run -- skipping the test-count floor."
    exit 0
fi

count=$(sed -n 's/^\[==========\] Running \([0-9][0-9]*\) test.*/\1/p' \
        test-output.txt | head -n 1)

if [ -z "$count" ]; then
    echo "ERROR: could not parse a test count from the runner output." >&2
    echo "       Expected a gtest '[==========] Running N tests ...' line." >&2
    exit 1
fi

echo "[honuware] tests executed: $count (floor: $MIN_EXPECTED_TESTS)"

if [ "$count" -lt "$MIN_EXPECTED_TESTS" ]; then
    echo "ERROR: only $count tests ran, expected >= $MIN_EXPECTED_TESTS." >&2
    echo "       The suite did not fully link: honuware_tests is a static lib and" >&2
    echo "       its TEST() objects were likely dropped. The fix is to link it with" >&2
    echo "       \$<LINK_LIBRARY:WHOLE_ARCHIVE,honuware_tests> -- NOT to lower the floor." >&2
    exit 1
fi

echo "[honuware] OK"
