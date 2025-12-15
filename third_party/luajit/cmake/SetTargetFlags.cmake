# This module exposes following variables to the project:
# * BUILDVM_MODE
# * TARGET_C_FLAGS
# * TARGET_VM_FLAGS
# * TARGET_BIN_FLAGS
# * TARGET_SHARED_FLAGS
# * TARGET_LIBS

include(CheckUnwindTables)

# Clang does not recognize comment markers.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU"
    AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.1")
  AppendFlags(TARGET_C_FLAGS -Wimplicit-fallthrough)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(BUILDVM_MODE machasm)
else() # Linux and FreeBSD.
  set(BUILDVM_MODE elfasm)
endif()

LuaJITTestArch(TESTARCH "${TARGET_C_FLAGS}")
LuaJITArch(LUAJIT_ARCH "${TESTARCH}")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  AppendFlags(TARGET_C_FLAGS -DLUAJIT_UNWIND_EXTERNAL)
else()
  string(FIND ${TARGET_C_FLAGS} "LJ_NO_UNWIND 1" UNWIND_POS)
  if(UNWIND_POS EQUAL -1)
    # Find out whether the target toolchain always generates
    # unwind tables.
    CheckUnwindTables(HAVE_UNWIND_TABLES)
    if(HAVE_UNWIND_TABLES)
      AppendFlags(TARGET_C_FLAGS -DLUAJIT_UNWIND_EXTERNAL)
    endif()
  endif()
endif()

# Target-specific compiler options.
#
# x86/x64 only: For GCC 4.2 or higher and if you don't intend to
# distribute the binaries to a different machine you could also
# use: -march=native.
if(LUAJIT_ARCH STREQUAL "x86")
  AppendFlags(TARGET_C_FLAGS -march=i686 -msse -msse2 -mfpmath=sse -fno-omit-frame-pointer)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  AppendFlags(TARGET_SHARED_FLAGS -undefined dynamic_lookup)
else() # Linux and FreeBSD.
  AppendFlags(TARGET_BIN_FLAGS -Wl,-E)
  list(APPEND TARGET_LIBS dl)
endif()

# Auxiliary flags for the VM core.
# XXX: ASAN-related build flags are stored in CMAKE_C_FLAGS.
set(TARGET_VM_FLAGS "${CMAKE_C_FLAGS} ${TARGET_C_FLAGS}")

unset(LUAJIT_ARCH)
unset(TESTARCH)
