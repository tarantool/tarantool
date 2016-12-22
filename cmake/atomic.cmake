# Try compile a sample code using atomics and put success/failure
# status in the TARGET_VAR.
# Extra arguments (if any) are passed as compiler flags.
# Note: CMAKE_C_FLAGS overrides any flag passed this way. The intention
# is to avoid changing the architecture if it was set explicitly in
# CMake invocation.
function(check_cc_atomics TARGET_VAR)
    set(CMAKE_REQUIRED_INCLUDES ${CMAKE_SOURCE_DIR}/src/lib/small/third_party)
    set(CMAKE_REQUIRED_FLAGS "${ARGN} ${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS "")
    check_c_source_compiles("
    #include <pmatomic.h>
    int counter;
    int main() {
        return pm_atomic_fetch_add(&counter, 42);
    }" ${TARGET_VAR})
endfunction()

check_cc_atomics(CC_HAS_ATOMICS)

if (CC_HAS_ATOMICS)
    return()
endif()

# Atomics support missing with the default flags.
# If the failure was due to the compiler targeting an outdated CPU
# without atomic instructions we will fix it now.
# The precise flags depend on the processor architecture and the
# compiler being used.

# x86 (32 bit)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-9]86$")
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
        set(CC_ATOMICS_WORKAROUND_FLAGS "-march=i686")
    endif()
endif()

# Attempt to compile the same code again with the extra flags.
check_cc_atomics(
    CC_ATOMICS_WORKAROUND_WORKS ${CC_ATOMICS_WORKAROUND_FLAGS})

if (CC_ATOMICS_WORKAROUND_WORKS)
    message(STATUS "Enabling atomics (${CC_ATOMICS_WORKAROUND_FLAGS})")
    add_compile_flags("C;CXX" ${CC_ATOMICS_WORKAROUND_FLAGS})
else()
    message(FATAL_ERROR "C atomics not supported")
endif()

