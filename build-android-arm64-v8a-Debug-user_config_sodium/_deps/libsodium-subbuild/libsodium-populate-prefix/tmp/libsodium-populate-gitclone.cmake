# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

if(EXISTS "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitclone-lastrun.txt" AND EXISTS "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitinfo.txt" AND
  "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitclone-lastrun.txt" IS_NEWER_THAN "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitinfo.txt")
  message(VERBOSE
    "Avoiding repeated git clone, stamp file is up to date: "
    "'C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitclone-lastrun.txt'"
  )
  return()
endif()

# Even at VERBOSE level, we don't want to see the commands executed, but
# enabling them to be shown for DEBUG may be useful to help diagnose problems.
cmake_language(GET_MESSAGE_LOG_LEVEL active_log_level)
if(active_log_level MATCHES "DEBUG|TRACE")
  set(maybe_show_command COMMAND_ECHO STDOUT)
else()
  set(maybe_show_command "")
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E rm -rf "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium/e5e8"
  RESULT_VARIABLE error_code
  ${maybe_show_command}
)
if(error_code)
  message(FATAL_ERROR "Failed to remove directory: 'C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium/e5e8'")
endif()

# try the clone 3 times in case there is an odd git clone issue
set(error_code 1)
set(number_of_tries 0)
while(error_code AND number_of_tries LESS 3)
  execute_process(
    COMMAND "C:/Program Files/Git/cmd/git.exe"
            clone --no-checkout --depth 1 --no-single-branch --config "advice.detachedHead=false" "https://github.com/jedisct1/libsodium.git" "e5e8"
    WORKING_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium"
    RESULT_VARIABLE error_code
    ${maybe_show_command}
  )
  math(EXPR number_of_tries "${number_of_tries} + 1")
endwhile()
if(number_of_tries GREATER 1)
  message(NOTICE "Had to git clone more than once: ${number_of_tries} times.")
endif()
if(error_code)
  message(FATAL_ERROR "Failed to clone repository: 'https://github.com/jedisct1/libsodium.git'")
endif()

execute_process(
  COMMAND "C:/Program Files/Git/cmd/git.exe"
          checkout "master" --
  WORKING_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium/e5e8"
  RESULT_VARIABLE error_code
  ${maybe_show_command}
)
if(error_code)
  message(FATAL_ERROR "Failed to checkout tag: 'master'")
endif()

set(init_submodules TRUE)
if(init_submodules)
  execute_process(
    COMMAND "C:/Program Files/Git/cmd/git.exe" 
            submodule update --recursive --init 
    WORKING_DIRECTORY "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium/e5e8"
    RESULT_VARIABLE error_code
    ${maybe_show_command}
  )
endif()
if(error_code)
  message(FATAL_ERROR "Failed to update submodules in: 'C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/cpm.cache/libsodium/e5e8'")
endif()

# Complete success, update the script-last-run stamp file:
#
execute_process(
  COMMAND ${CMAKE_COMMAND} -E copy "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitinfo.txt" "C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitclone-lastrun.txt"
  RESULT_VARIABLE error_code
  ${maybe_show_command}
)
if(error_code)
  message(FATAL_ERROR "Failed to copy script-last-run stamp file: 'C:/Users/nickc/Projects/aether-client-cpp/build-android-arm64-v8a-Debug-user_config_sodium/_deps/libsodium-subbuild/libsodium-populate-prefix/src/libsodium-populate-stamp/libsodium-populate-gitclone-lastrun.txt'")
endif()
