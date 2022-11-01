# Assuming the C and C++ compiler IDs are equal.
# For clang `MATCHES` is used to match both Clang and AppleClang.
# `VERSION_GREATER` instead of `VERSION_GREATER_EQUAL` for compatibility
# with cmake 3.5 on ubuntu 16.04.
if ((CMAKE_C_COMPILER_ID MATCHES "Clang" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "9" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "9")) OR
    (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER "8" OR CMAKE_C_COMPILER_VERSION VERSION_EQUAL "8")))
    set(PREFIX_MAP_FLAGS "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()
