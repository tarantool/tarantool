if (NOT CMAKE_SYSTEM_PROCESSOR)
    message(FATAL_ERROR "Missing CMAKE_SYSTEM_PROCESSOR. "
        "Please ensure that CMake is installed properly or "
        "add CMAKE_SYSTEM_PROCESSOR into your toolchain file.")
endif ()

test_big_endian(HAVE_BYTE_ORDER_BIG_ENDIAN)
#
# We do not perform host-to-network byte order translation,
# and simply assume the machine is little-endian.
# We also do not bother with trying to avoid unaligned
# word access. Refuse to compile on rare hardware such as
# Sparc or Itanium.
#
if (${HAVE_BYTE_ORDER_BIG_ENDIAN} OR
    ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "sparc" OR
    ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "ia64" OR
    ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^alpha")
    message (FATAL_ERROR "Unsupported architecture -- ${CMAKE_SYSTEM_PROCESSOR}, ")
    message (FATAL_ERROR "Tarantool currently only supports little-endian hardware")
    message (FATAL_ERROR "with unaligned word access.")
endif()

#
# Bug in CMake, Darwin always detect on i386
# Fixed with check types
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    if (CMAKE_SIZEOF_VOID_P MATCHES 8)
        set(CMAKE_SYSTEM_PROCESSOR "x86_64")
    else(CMAKE_SIZEOF_VOID_P MATCHES 8)
        set(CMAKE_SYSTEM_PROCESSOR "x86")
    endif(CMAKE_SIZEOF_VOID_P MATCHES 8)
endif()
