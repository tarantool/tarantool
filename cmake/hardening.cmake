# Depends on os.cmake and profile.cmake modules.
# Uses `ENABLE_FUZZER` option and `TARGET_OS_DARWIN` variable.

# ligomp.a for AArch64 CentOS is compiled without PIC support.
if (ENABLE_FUZZER OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
    set(ENABLE_HARDENING_DEFAULT FALSE)
else()
    set(ENABLE_HARDENING_DEFAULT TRUE)
endif()
option(ENABLE_HARDENING "Enable compiler options that harden against memory corruption attacks" ${ENABLE_HARDENING_DEFAULT})
if (ENABLE_HARDENING)
    set(HARDENING_FLAGS "-Wformat -Wformat-security -Werror=format-security -fstack-protector-strong -fPIC")
    if (NOT TARGET_OS_DARWIN)
        set(HARDENING_LDFLAGS "-pie -z relro -z now")
    endif()
endif()
