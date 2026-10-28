# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/libbcrypt/bb48")
  file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/libbcrypt/bb48")
endif()
file(MAKE_DIRECTORY
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-build"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/tmp"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/src/libbcrypt-populate-stamp"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/src"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/src/libbcrypt-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/src/libbcrypt-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/_deps/libbcrypt-subbuild/libbcrypt-populate-prefix/src/libbcrypt-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
