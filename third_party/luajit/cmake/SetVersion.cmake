# Find, check and set LuaJIT's version from a VCS tag.
# Major portions taken verbatim or adapted from the uJIT.
# Copyright (C) 2020-2021 LuaVela Authors.
# Copyright (C) 2015-2020 IPONWEB Ltd.

function(SetVersion version majver minver patchver tweakver prerel)
  find_package(Git QUIET REQUIRED)
  if(EXISTS ${CMAKE_SOURCE_DIR}/.git AND Git_FOUND)
    # Read version from the project's VCS and store the result
    # into version.
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      OUTPUT_VARIABLE vcs_tag)
    string(STRIP "${vcs_tag}" vcs_tag)
    message(STATUS "[SetVersion] Reading version from VCS: ${vcs_tag}")
  else()
    # Use default version since no git is found in the system or
    # VCS directory is not found in the repo root directory.
    set(vcs_tag "v2.1.0-beta3-0-g0000000")
    message(STATUS "[SetVersion] No VCS found, use default version: ${vcs_tag}")
  endif()

  set(${version} ${vcs_tag} PARENT_SCOPE)

  # Match version_string against the version regex.
  # Throw an error if it does not match. Otherwise populates
  # variables:
  # * majver:   First version number.
  # * minver:   Second version number.
  # * patchver: Third version number.
  # * prerel:   Optional prerelease suffix.
  # * tweakver: Fourth version number.
  # Valid version examples:
  # * v2.0.4-48-gfcc8244
  # * v2.1.0-beta3-57-g2973518
  if(vcs_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-(rc|beta)[0-9]+)?-([0-9]+)-g[0-9a-z]+$")
    set(${majver}   ${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${minver}   ${CMAKE_MATCH_2} PARENT_SCOPE)
    set(${patchver} ${CMAKE_MATCH_3} PARENT_SCOPE)
    set(${tweakver} ${CMAKE_MATCH_6} PARENT_SCOPE)
    set(${prerel}   ${CMAKE_MATCH_4} PARENT_SCOPE)
  else()
    message(FATAL_ERROR "[SetVersion] Malformed version string '${vcs_tag}'")
  endif()
endfunction()
