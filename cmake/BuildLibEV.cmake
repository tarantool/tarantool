#
# A macro to build the bundled libev
macro(libev_build)
    set(ev_compile_flags)
    set(ev_link_libraries)
# There are warnings in libev code which are impossible to selectively
# turn off, see
# http://gcc.gnu.org/bugzilla/show_bug.cgi?id=45977
# http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMPILER_WARNINGS
# while this stand off is going on, the world is not a very happy
# place:
    set(ev_compile_flags "${ev_compile_flags} -w")

    set(ev_compile_flags "${ev_compile_flags} -DENABLE_BUNDLED_LIBEV=1")

    if (TARGET_OS_LINUX)
        #
        # Enable Linux-specific event notification API (man inotify)
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_INOTIFY")
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_EVENTFD")
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_SIGNALFD")
    elseif (TARGET_OS_FREEBSD OR TARGET_OS_NETBSD OR TARGET_OS_DARWIN)
        #
        # On FreeBSD build libev loop on top of
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_KQUEUE")
    endif()

    list(APPEND ev_link_libraries "m")
    if (TARGET_OS_DEBIAN_FREEBSD)
        # libev depends on librt under kFreeBSD
        list(APPEND ev_link_libraries "rt")
    else()

    endif()

    set(libev_src
        ${PROJECT_SOURCE_DIR}/third_party/tarantool_ev.c
    )

    add_library(ev STATIC ${libev_src})

    set_target_properties(ev PROPERTIES COMPILE_FLAGS "${ev_compile_flags}")
    target_link_libraries(ev ${ev_link_libraries})

    set(LIBEV_INCLUDE_DIR ${PROJECT_BINARY_DIR}/third_party)
    set(LIBEV_LIBRARIES ev)

    unset(ev_src)
    unset(ev_compile_flags)
    unset(ev_link_libraries)
endmacro(libev_build)
