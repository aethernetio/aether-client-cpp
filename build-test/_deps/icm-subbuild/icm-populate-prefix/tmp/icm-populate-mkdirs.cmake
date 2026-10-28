# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/icm/4773")
  file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/icm/4773")
endif()
file(MAKE_DIRECTORY
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-build"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/tmp"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/src"
  "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/angel/projects/aether-client-cpp/build-test/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
