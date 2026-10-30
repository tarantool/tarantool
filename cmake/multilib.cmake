if(DEFINED MULTILIB)
    return()
endif()

set(_MULTILIB lib)
# Comment from GNUInstallDirs.cmake:
# Override this default 'lib' with 'lib64' iff:
#  - we are on Linux system but NOT cross-compiling
#  - we are NOT on debian
#  - we are on a 64 bits system
# reason is: amd64 ABI: http://www.x86-64.org/documentation/abi.pdf
# For Debian with multiarch, use 'lib/${CMAKE_LIBRARY_ARCHITECTURE}' if
# CMAKE_LIBRARY_ARCHITECTURE is set (which contains e.g. "i386-linux-gnu"
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU)$" AND
   NOT CMAKE_CROSSCOMPILING)
    if (EXISTS "/etc/debian_version" AND CMAKE_LIBRARY_ARCHITECTURE)
        # Debian
        set(_MULTILIB "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    elseif(DEFINED CMAKE_SIZEOF_VOID_P AND "${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        # Not debian, rely on CMAKE_SIZEOF_VOID_P:
        set(_MULTILIB "lib64")
    endif()
endif()

set(MULTILIB "${_MULTILIB}" CACHE PATH "multilib suffix")
