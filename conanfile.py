from conan import ConanFile
from conan.tools.microsoft import vs_layout

import os
from typing import NamedTuple, Optional

class CMakeInfo(NamedTuple):
    # Name of the package to find with find_package
    package: str
    # CMake target to use with target_link_libraries, if needed
    target: str
    # CMake variable to use in CMakeLists, defaults to ucase "${package}_LIB"
    var: Optional[str] = None

class Library(NamedTuple):
    # Name of library from conan-center
    name: str
    # Version of package from conan-center
    version: str
    # CMake package info
    info: Optional[CMakeInfo] = None


# Third-party deps used by the honuware components only. The app/CLI-only deps
# from the knottyyoga recipe (ftxui, replxx — the test-helper TUI) are dropped.
libraries = [
    # DO NOT go back below 2025.x. Like libtiff 4.6.0, the 20220623.1 recipe
    # tool_requires cmake/[>=3.16 <4], forcing a CMake 3.x that cannot emit the
    # "Visual Studio 18 2026" generator -- it was the second of the two hard
    # VS2026 blockers. Note honuware itself never links ${ABSL_LIB}; the entry is
    # here only to keep the app conanfiles a strict superset of this one.
    Library("abseil", "20250814.2", CMakeInfo("absl", "abseil::abseil")),
    # boost and mailio MOVE TOGETHER. mailio pins boost EXACTLY (not a range):
    # 0.25.3 -> boost/1.86.0, 0.26.0 -> boost/1.91.0. That pin, not a preference,
    # is why boost sits at 1.86. Changing one without the other fails graph
    # resolution with a version conflict before anything compiles.
    #
    # HELD at 1.86.0 / 0.25.3 DELIBERATELY (VS2026 migration, Phase 5.1). The
    # 1.91.0 / 0.26.0 pair was tried and reverted: it compiles and links, but
    # SEGFAULTS in the real SMTP send path -- MailHelperTest.SendMessage dies
    # inside mailio's smtps connect/submit, where 0.25.3 completes. A crash in
    # the mail path is a worse trade than an old mailio.
    #
    # Neither version is required for VS2026: the b2 floor in
    # conan/profiles/windows is what makes Boost build on the v145 toolset.
    #
    # The CODE is already adapted for 1.91, so this pin can move the moment the
    # mailio crash is understood. Two things were fixed and kept:
    #   - util/thread_pool.h no longer leaks Boost.Asio into its includers. From
    #     1.91, Boost.Asio and crow's standalone Asio share global helper
    #     namespaces and cannot coexist in one translation unit.
    #   - the scheduler no longer calls basic_waitable_timer::cancel(error_code&),
    #     which 1.91 removed.
    Library("boost", "1.86.0", CMakeInfo("Boost", "boost::boost")),
    Library("crowcpp-crow", "1.3.3", CMakeInfo("Crow", "Crow::Crow")),
    Library("date", "3.0.5", CMakeInfo("date", "date::date")),
    # 1.17 requires C++17; we build at 20. The only exposure in this repo is the
    # two custom matchers in components/testing/test/src/util (JsonWvalueMatcher,
    # PostGresResultMatcher), which use ::testing::MatcherInterface / MakeMatcher
    # -- still the supported custom-matcher API. None of the macros gtest removed
    # (INSTANTIATE_TEST_CASE_P, TYPED_TEST_CASE, ::testing::TestCase) appear
    # anywhere here; the no-fixtures convention is what keeps that surface small.
    Library("gtest", "1.17.0", CMakeInfo("GTest", "gtest::gtest")),
    # 7.86.0 -> 8.21.0 looks like a major break and is not: curl kept its API
    # across the 8 boundary, and CURL::libcurl is unchanged.
    Library("libcurl", "8.21.0", CMakeInfo("CURL", "CURL::libcurl")),
    Library("libjpeg", "9f"),
    Library("libpng", "1.6.58", CMakeInfo("PNG", "PNG::PNG")),
    Library("libpqxx", "7.10.5", CMakeInfo("libpqxx", "libpqxx::pqxx", "PQXX_LIB")),
    Library("libsodium", "1.0.20", CMakeInfo("libsodium", "libsodium::libsodium")),
    # DO NOT go back below 4.7.x. The 4.6.0 recipe tool_requires cmake/[>=3.18 <4],
    # which forces Conan to fetch a CMake 3.x -- and CMake 3.x cannot emit the
    # "Visual Studio 18 2026" generator, so 4.6.0 makes the whole graph unbuildable
    # on VS2026. One of the two hard VS2026 blockers (abseil is the other).
    Library("libtiff", "4.7.2", CMakeInfo("TIFF", "TIFF::TIFF")),
    # Tenant Theming Phase 9 — theme bundles travel as a .zip through the admin
    # page. Deliberately not hand-rolled: the READER parses untrusted input,
    # which is the wrong place to save a dependency.
    Library("libzip", "1.11.4", CMakeInfo("libzip", "libzip::zip")),
    Library("mailio", "0.25.3", CMakeInfo("mailio", "mailio::mailio")),
    # NOTE the capitalisation: Conan's openssl recipe sets cmake_file_name="OpenSSL",
    # so CMakeDeps emits OpenSSLConfig.cmake. find_package() filename lookup is
    # case-SENSITIVE on Linux, so find_package(openssl) fails there with
    # "Could not find a package configuration file provided by openssl" -- while
    # working fine on Windows, whose filesystem is case-insensitive and matches
    # OpenSSLConfig.cmake anyway. Every other entry below already matches its
    # generated filename exactly; this was the only mismatch. The linked target stays
    # lowercase openssl::openssl (that IS what OpenSSLConfig.cmake declares, alongside
    # OpenSSL::SSL and OpenSSL::Crypto), and ${OPENSSL_LIB} is unchanged because the
    # generator upper-cases the package name either way.
    Library("openssl", "3.5.8", CMakeInfo("OpenSSL", "openssl::openssl")),
    Library("zlib", "1.3.2", CMakeInfo("ZLIB", "ZLIB::ZLIB")),
]

class HonuwareRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def init(self):
        # emit the find_package calls in the conanbuildinfo.cmake,
        # along with the variables for later use in the CMakeLists
        with open(os.path.join(self.recipe_folder, "ConanLibImports.cmake"), "w") as f:
            f.write('# Generated file: DO NOT EDIT or COMMIT!\n')
            f.write('# Add your library to the conanfile.py libraries list\n\n')
            for library in libraries:
                if library.info:
                  f.write(f'find_package({library.info.package} REQUIRED)\n')
                  var_name = library.info.var if library.info.var else f'{library.info.package.upper()}_LIB'
                  f.write(f'set({var_name} {library.info.target})\n\n')

    def requirements(self):
        for requirement in libraries:
            self.requires(requirement.name+"/"+requirement.version)

    def layout(self):
        vs_layout(self)
