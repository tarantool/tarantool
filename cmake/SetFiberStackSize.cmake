# This module provides a CMake configuration variable to set
# fiber stack size. If no value is given the default size equals
# to 512Kb (see https://github.com/tarantool/tarantool/issues/3418
# for more info). Possible types of values are described below.
# The given size is converted to bytes and aligned up to be
# multiple of 4Kb. As a result the corresponding define value is
# propagated to the sources.

set(FIBER_STACK_SIZE "524288" CACHE STRING "Fiber stack size")

# Possible values of the stack size:
# * Just a bunch of digits: the size is given in bytes
# * Digits with "KB"|"Kb" suffix: the size is given in kilobytes
# * Digits with "MB"|"Mb" suffix: the size is given in megabytes
# All other values are considered as invalid.
if(FIBER_STACK_SIZE MATCHES "^([0-9]+)$")
    set(FIBER_STACK_SIZE_IN_BYTES ${CMAKE_MATCH_1})
elseif(FIBER_STACK_SIZE MATCHES "([0-9]+)K[bB]$")
    math(EXPR FIBER_STACK_SIZE_IN_BYTES "${CMAKE_MATCH_1} << 10")
elseif(FIBER_STACK_SIZE MATCHES "([0-9]+)M[bB]$")
    math(EXPR FIBER_STACK_SIZE_IN_BYTES "${CMAKE_MATCH_1} << 20")
else()
    message(FATAL_ERROR "Invalid size of the fiber stack")
endif()

# XXX: Align the stack size in bytes up to be multiple of 4Kb.
math(EXPR FIBER_STACK_SIZE_IN_BYTES
    "((${FIBER_STACK_SIZE_IN_BYTES} >> 12) + 1) << 12")

# See the rationale for the minimal fiber stack size in
# https://github.com/tarantool/tarantool/issues/3418.
if(FIBER_STACK_SIZE_IN_BYTES LESS 524288)
    message(FATAL_ERROR "[SetFiberStackSize] Minimal fiber stack size is 512Kb,"
        " but ${FIBER_STACK_SIZE} is given as a default")
else()
    message(STATUS "[SetFiberStackSize] Default fiber stack size: "
        " ${FIBER_STACK_SIZE} (adjusted to ${FIBER_STACK_SIZE_IN_BYTES} bytes)")
endif()

# Propagate the value to the sources.
add_definitions(-DFIBER_STACK_SIZE_DEFAULT=${FIBER_STACK_SIZE_IN_BYTES})

# XXX: Unset variables to avoid spoiliing CMake environment.
unset(FIBER_STACK_SIZE_IN_BYTES)
unset(FIBER_STACK_SIZE)
