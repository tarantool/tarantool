# Uses the `DEV_BUILD` option.

# Assuming the C and C++ compiler IDs are equal.
# For clang `MATCHES` is used to match both Clang and AppleClang.
# `VERSION_GREATER` instead of `VERSION_GREATER_EQUAL` for compatibility
# with cmake 3.5 on ubuntu 16.04.
if ((CMAKE_C_COMPILER_ID MATCHES "Clang" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "9" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "9")) OR
    (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "8" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "8")))
    set(PREFIX_MAP_FLAGS "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

# Assuming the C and C++ compiler IDs are equal.
# For clang `MATCHES` is used to match both Clang and AppleClang.
# Since this flag requires an extra command to tell the debugger where to find
# the source files, which is not convenient for developers, it is disabled when
# `DEV_BUILD` option is turned on.
if (NOT DEV_BUILD AND
    # `VERSION_GREATER` instead of `VERSION_GREATER_EQUAL` for compatibility
    # with cmake 3.5 on ubuntu 16.04.
    ((CMAKE_C_COMPILER_ID MATCHES "Clang" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "3.8" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "3.8")) OR
    (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "4.4.7" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "4.4.7"))))
    # Clang integrated assembler does not support this option.
    set(CMAKE_REQUIRED_FLAGS "-Wa,--debug-prefix-map")
    check_c_source_compiles("int main(void) {}" HAVE_AS_DEBUG_PREFIX_MAP)
    unset(CMAKE_REQUIRED_FLAGS)
    if (HAVE_WA_DEBUG_PREFIX_MAP)
        set(AS_DEBUG_PREFIX_MAP_FLAG "-Wa,--debug-prefix-map=${CMAKE_SOURCE_DIR}=.")
    endif()
    set(PREFIX_MAP_FLAGS "${PREFIX_MAP_FLAGS} -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=. ${AS_DEBUG_PREFIX_MAP_FLAG}")
endif()
