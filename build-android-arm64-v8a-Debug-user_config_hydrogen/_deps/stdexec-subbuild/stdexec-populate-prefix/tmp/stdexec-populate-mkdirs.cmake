# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/cpm.cache/stdexec/ff1b")
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/cpm.cache/stdexec/ff1b")
endif()
file(MAKE_DIRECTORY
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-build"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/tmp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/src/stdexec-populate-stamp"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/src"
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/src/stdexec-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/src/stdexec-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_hydrogen/_deps/stdexec-subbuild/stdexec-populate-prefix/src/stdexec-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
