#
# Manage LTO (Link-Time-Optimization) and IPO
# (Inter-Procedural-Optimization)
#

# Tarantool uses both dynamic-list and lto link options, which
# works only since binutils:
#
# - 2.30 for linking with ld.gold (gold version is 1.15);
# - last 2.30 or 2.31 in case of ld.bfd.

# This cmake module exports CMP0069 policy and should be included
# with NO_POLICY_SCOPE option.

# The file gives an error if LTO is requested, but cannot be
# enabled for some reason.

if (NOT DEFINED ENABLE_LTO)
    set(ENABLE_LTO OFF)
endif()

# Disable LTO if not requested.
if (NOT ENABLE_LTO)
    message(STATUS "Enabling LTO: FALSE")
    return()
endif()

if(CMAKE_VERSION VERSION_LESS 3.9)
    message(FATAL_ERROR "cmake >= 3.9 is needed to enable LTO")
endif()

# 'CMP0069 NEW' behaviour enables LTO for compilers other then
# Intel Compiler when CMAKE_INTERPROCEDURAL_OPTIMIZATION is
# enabled and provides CheckIPOSupported module. We set the policy
# to support LTO with GCC / Clang and to suppress cmake warnings
# on the unset policy.
cmake_policy(SET CMP0069 NEW)

# Retain 'CMP0069 NEW' behaviour after
# 'cmake_minimum_required(VERSION ...) in subprojects to
# avoid cmake warnings on the unset policy.
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

# Check whether LTO is supported by the compiler / toolchain and
# give an error otherwise.
include(CheckIPOSupported)
check_ipo_supported(RESULT CMAKE_IPO_AVAILABLE)
if (NOT CMAKE_IPO_AVAILABLE)
    message(FATAL_ERROR "LTO is not supported by the compiler / toolchain")
endif()

# Extra checks on Linux whether all needed LTO features are
# supported. Mac OS seems to work correctly with xcode >= 8.
if (NOT TARGET_OS_DARWIN)
    execute_process(
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND ld -v OUTPUT_VARIABLE linker_version_str)
    message(STATUS "ld version string: ${linker_version_str}")

    # GNU ld (Gentoo 2.31.1 p3) 2.31.1
    # GNU ld (GNU Binutils for Ubuntu) 2.30
    # GNU ld version 2.27-10.el7
    string(REGEX MATCH "^GNU ld.* (2\\.[0-9]+)[^ ]*$" matched_bfd
        ${linker_version_str})

    # GNU gold (Gentoo 2.31.1 p3 2.31.1) 1.16
    # GNU gold (GNU Binutils for Ubuntu 2.30) 1.15
    # GNU gold (version 2.27-10.el7) 1.12
    if (NOT matched_bfd)
        string(REGEX MATCH "^GNU gold.* (1\\.[0-9]+)[^ ]*$" matched_gold
            ${linker_version_str})
    endif()

    if(matched_bfd)
        set(linker_version ${CMAKE_MATCH_1})
        message(STATUS "Found ld.bfd version: ${linker_version}")

        if (linker_version VERSION_LESS "2.31")
            message(FATAL_ERROR "ld.bfd >= 2.31 is needed for LTO")
        endif()
    elseif(matched_gold)
        set(linker_version ${CMAKE_MATCH_1})
        message(STATUS "Found ld.gold version: ${linker_version}")

        if (linker_version VERSION_LESS "1.15")
            message(FATAL_ERROR "ld.gold >= 1.15 is needed for LTO")
        endif()
    else()
        message(FATAL_ERROR "Unsupported ld version format")
    endif()
endif()

# gh-3742: investigate LTO warnings.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wno-lto-type-mismatch")

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
message(STATUS "Enabling LTO: TRUE")
