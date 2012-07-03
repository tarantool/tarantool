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
