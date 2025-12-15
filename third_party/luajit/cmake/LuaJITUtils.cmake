function(LuaJITTestArch outvar strflags)
  # XXX: This routine uses external headers (e.g. system ones),
  # which location is specified either implicitly (within CMake
  # machinery) or explicitly (manually by configuration options).
  # Need -isysroot flag on recentish MacOS after command line
  # tools no longer provide headers in /usr/include.
  # XXX: According to CMake documentation[1], CMAKE_OSX_SYSROOT
  # variable *should* be ignored on the platforms other than
  # MacOS. It is ignored by CMake itself, but since this routine
  # extends CMake, it should also follow this policy.
  # [1]: https://cmake.org/cmake/help/v3.1/variable/CMAKE_OSX_SYSROOT.html
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_SYSROOT)
    set(strflags "${strflags} -isysroot ${CMAKE_OSX_SYSROOT}")
  endif()
  # XXX: <execute_process> simply splits the COMMAND argument by
  # spaces with no further parsing. At the same time GCC is bad in
  # argument handling, so let's help it a bit.
  separate_arguments(TESTARCH_C_FLAGS UNIX_COMMAND ${strflags})
  # TODO: It would be nice to drop a few words, why do we use this
  # approach instead of CMAKE_HOST_SYSTEM_PROCESSOR variable.
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} ${TESTARCH_C_FLAGS} -E lj_arch.h -dM
    WORKING_DIRECTORY ${LUAJIT_SOURCE_DIR}
    OUTPUT_VARIABLE TESTARCH
    RESULT_VARIABLE TESTARCH_RC
  )
  if(TESTARCH_RC EQUAL 0)
    set(${outvar} ${TESTARCH} PARENT_SCOPE)
  else()
    # XXX: Yield a special value (an empty string) to respect the
    # failed preprocessor routine and then fail arch detection.
    set(${outvar} "" PARENT_SCOPE)
  endif()
endfunction()

function(LuaJITArch outvar testarch)
  # XXX: Please do not change the order of the architectures.
  foreach(TRYARCH X64 X86 ARM64 ARM PPC MIPS64 MIPS)
    string(FIND "${testarch}" "LJ_TARGET_${TRYARCH}" FOUND)
    # FIXME: <continue> is introduced in CMake version 3.2, but
    # the minimum required version now is 3.1. This is not such a
    # vital feature, so it's not used now. However, when CMake
    # version is bumped next time, it's better to rewrite this
    # part using <continue> for "early return".
    # For more info, see CMake Release notes for 3.2 version.
    # https://cmake.org/cmake/help/latest/release/3.2.html#commands
    if(NOT FOUND EQUAL -1)
      string(TOLOWER ${TRYARCH} LUAJIT_ARCH)
      set(${outvar} ${LUAJIT_ARCH} PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "[LuaJITArch] Unsupported target architecture")
endfunction()

macro(AppendFlags flags)
  foreach(flag ${ARGN})
    set(${flags} "${${flags}} ${flag}")
  endforeach()
endmacro()
