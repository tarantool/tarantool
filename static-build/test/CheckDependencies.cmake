## This is a cmake-based test, it checks that tarantool static binary
# has no dependencies except allowed ones.

include(GetPrerequisites)
if(NOT FILE)
    message(FATAL_ERROR "Usage: "
        "${CMAKE_COMMAND} -DFILE=<FILENAME> -P CheckDependencies.cmake")
elseif(NOT EXISTS ${FILE})
    message(FATAL_ERROR "${FILE}: No such file")
endif()

get_prerequisites(${FILE} DEPENDENCIES 0 0 "" "")

if (APPLE)
    set(ALLOWLIST
        libSystem
        CoreFoundation
        libc++
    )
elseif(UNIX)
    set(ALLOWLIST
        libdl
        librt
        libc
        libm
        libgcc_s
        libpthread
    )
else()
    message(FATAL_ERROR "Unknown platform")
endif()

foreach(DEPENDENCY_FILE ${DEPENDENCIES})
    message("Dependency: ${DEPENDENCY_FILE}")
endforeach()

foreach(DEPENDENCY_FILE ${DEPENDENCIES})
    get_filename_component(libname ${DEPENDENCY_FILE} NAME_WE)
    list (FIND ALLOWLIST ${libname} _index)
    if (_index EQUAL -1)
        message(FATAL_ERROR "Blocklisted dependency: ${DEPENDENCY_FILE}")
    endif()
endforeach()
