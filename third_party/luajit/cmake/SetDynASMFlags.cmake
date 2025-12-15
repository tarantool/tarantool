# This module exposes following variables to the project:
# * HOST_C_FLAGS
# * DYNASM_ARCH
# * DYNASM_FLAGS

# XXX: buildvm includes core headers and thus has to be built
# with the same flags and defines as the LuaJIT core itself.
set(HOST_C_FLAGS)
set(DYNASM_ARCH)
set(DYNASM_FLAGS)

LuaJITTestArch(TESTARCH "${TARGET_C_FLAGS} ${HOST_CFLAGS}")
LuaJITArch(LUAJIT_ARCH "${TESTARCH}")
AppendFlags(HOST_C_FLAGS -DLUAJIT_TARGET=LUAJIT_ARCH_${LUAJIT_ARCH})

# XXX: LUAJIT_ARCH equals to DYNASM_ARCH for the most cases, but
# there are few exceptions to the rule.
if(LUAJIT_ARCH STREQUAL "x64")
  string(FIND "${TESTARCH}" "LJ_FR2 1" FOUND)
  if(FOUND EQUAL -1)
    set(DYNASM_ARCH x86)
  else()
    set(DYNASM_ARCH x64)
  endif()
elseif(LUAJIT_ARCH STREQUAL "ppc")
  string(FIND "${TESTARCH}" "LJ_ARCH_PPC64" FOUND)
  if(FOUND EQUAL -1)
    set(DYNASM_ARCH ppc)
  else()
    set(DYNASM_ARCH ppc64)
  endif()
else()
  set(DYNASM_ARCH ${LUAJIT_ARCH})
endif()

if(LUAJIT_ARCH STREQUAL "arm64")
  string(FIND "${TESTARCH}" "__AARCH64EB__" FOUND)
  if(NOT FOUND EQUAL -1)
    AppendFlags(HOST_C_FLAGS -D__AARCH64EB__=1)
  endif()
elseif(LUAJIT_ARCH STREQUAL "ppc")
  string(FIND "${TESTARCH}" "LJ_LE 1" FOUND)
  if(NOT FOUND EQUAL -1)
    AppendFlags(HOST_C_FLAGS -DLJ_ARCH_ENDIAN=LUAJIT_LE)
  else()
    AppendFlags(HOST_C_FLAGS -DLJ_ARCH_ENDIAN=LUAJIT_BE)
  endif()
  string(FIND "${TESTARCH}" "LJ_ARCH_SQRT 1" FOUND)
  if(NOT FOUND EQUAL -1)
    list(APPEND DYNASM_FLAGS -D SQRT)
  endif()
  string(FIND "${TESTARCH}" "LJ_ARCH_ROUND 1" FOUND)
  if(NOT FOUND EQUAL -1)
    list(APPEND DYNASM_FLAGS -D ROUND)
  endif()
  string(FIND "${TESTARCH}" "LJ_ARCH_PPC32ON64 1" FOUND)
  if(NOT FOUND EQUAL -1)
    list(APPEND DYNASM_FLAGS -D GPR64)
  endif()
elseif(LUAJIT_ARCH STREQUAL "mips")
  string(FIND "${TESTARCH}" "MIPSEL" FOUND)
  if(NOT FOUND EQUAL -1)
    AppendFlags(HOST_C_FLAGS -D__MIPSEL__=1)
  endif()
endif()

string(FIND "${TESTARCH}" "LJ_TARGET_MIPSR6" FOUND)
if(NOT FOUND EQUAL -1)
  AppendFlags(DYNASM_FLAGS -D MIPSR6)
endif()

string(FIND "${TESTARCH}" "LJ_LE 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D ENDIAN_LE)
else()
  list(APPEND DYNASM_FLAGS -D ENDIAN_BE)
endif()

string(FIND "${TESTARCH}" "LJ_ARCH_BITS 64" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D P64)
endif()

string(FIND "${TESTARCH}" "LJ_HASJIT 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D JIT)
endif()

string(FIND "${TESTARCH}" "LJ_HASFFI 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D FFI)
endif()

string(FIND "${TESTARCH}" "LJ_DUALNUM 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D DUALNUM)
endif()

string(FIND "${TESTARCH}" "LJ_ARCH_HASFPU 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D FPU)
  AppendFlags(HOST_C_FLAGS -DLJ_ARCH_HASFPU=1)
else()
  AppendFlags(HOST_C_FLAGS -DLJ_ARCH_HASFPU=0)
endif()

string(FIND "${TESTARCH}" "LJ_ABI_SOFTFP 1" FOUND)
if(NOT FOUND EQUAL -1)
  AppendFlags(HOST_C_FLAGS -DLJ_ABI_SOFTFP=1)
else()
  list(APPEND DYNASM_FLAGS -D HFABI)
  AppendFlags(HOST_C_FLAGS -DLJ_ABI_SOFTFP=0)
endif()

string(FIND "${TESTARCH}" "LJ_NO_UNWIND 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D NO_UNWIND)
  AppendFlags(HOST_C_FLAGS -DLUAJIT_NO_UNWIND)
endif()

string(FIND "${TESTARCH}" "LJ_HASSYSPROF 1" FOUND)
if(NOT FOUND EQUAL -1)
  list(APPEND DYNASM_FLAGS -D LJ_HASSYSPROF)
endif()

string(REGEX MATCH "LJ_ARCH_VERSION ([0-9]+)" LUAJIT_ARCH_VERSION ${TESTARCH})
list(APPEND DYNASM_FLAGS -D VER=${CMAKE_MATCH_1})

if(NOT CMAKE_SYSTEM_NAME STREQUAL ${CMAKE_HOST_SYSTEM_NAME})
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    AppendFlags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_OSX)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    AppendFlags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_LINUX)
  else() # FreeBSD.
    AppendFlags(HOST_C_FLAGS -DLUAJIT_OS=LUAJIT_OS_OTHER)
  endif()
endif()

if(LUAJIT_USE_UBSAN)
  # XXX: Skip checks for now to avoid build failures due to
  # sanitizer errors.
  # Need to backport commits that fix the following issues first:
  # https://github.com/LuaJIT/LuaJIT/pull/969,
  # https://github.com/LuaJIT/LuaJIT/pull/970,
  # https://github.com/LuaJIT/LuaJIT/issues/1041,
  # https://github.com/LuaJIT/LuaJIT/pull/1044.
  AppendFlags(HOST_C_FLAGS -fno-sanitize=undefined)
endif()

unset(LUAJIT_ARCH)
unset(TESTARCH)
