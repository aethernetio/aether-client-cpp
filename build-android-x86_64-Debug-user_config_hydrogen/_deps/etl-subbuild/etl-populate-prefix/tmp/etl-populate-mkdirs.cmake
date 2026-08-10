# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/cpm.cache/etl/3be0")
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/cpm.cache/etl/3be0")
endif()
file(MAKE_DIRECTORY
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-build"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/tmp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/src/etl-populate-stamp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/src"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/src/etl-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/src/etl-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-x86_64-Debug-user_config_hydrogen/_deps/etl-subbuild/etl-populate-prefix/src/etl-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
