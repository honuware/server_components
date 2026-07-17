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
    Library("abseil", "20220623.1", CMakeInfo("absl", "abseil::abseil")),
    Library("boost", "1.86.0", CMakeInfo("Boost", "boost::boost")),
    Library("crowcpp-crow", "1.3.2", CMakeInfo("Crow", "Crow::Crow")),
    Library("date", "3.0.4", CMakeInfo("date", "date::date")),
    Library("gtest", "1.12.1", CMakeInfo("GTest", "gtest::gtest")),
    Library("libcurl", "7.86.0", CMakeInfo("CURL", "CURL::libcurl")),
    Library("libjpeg", "9e"),
    Library("libpng", "1.6.40", CMakeInfo("PNG", "PNG::PNG")),
    Library("libpqxx", "7.10.5", CMakeInfo("libpqxx", "libpqxx::pqxx", "PQXX_LIB")),
    Library("libsodium", "1.0.20", CMakeInfo("libsodium", "libsodium::libsodium")),
    Library("libtiff", "4.6.0", CMakeInfo("TIFF", "TIFF::TIFF")),
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
    Library("openssl", "3.5.2", CMakeInfo("OpenSSL", "openssl::openssl")),
    Library("zlib", "1.3.1", CMakeInfo("ZLIB", "ZLIB::ZLIB")),
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
