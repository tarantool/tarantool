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

#    if (CC_HAS_WNO_UNUSED_RESULT)
#        set(ev_compile_flags "${ev_compile_flags} -Wno-unused-value")
#    endif()
#    if (CC_HAS_WNO_COMMENT)
#        set(ev_compile_flags "${ev_compile_flags} -Wno-comment")
#    endif()
#    if (CC_HAS_FNO_STRICT_ALIASING)
#        set(ev_compile_flags "${ev_compile_flags} -fno-strict-aliasing")
#    endif()
#    if (CC_HAS_WNO_PARENTHESES)
#        set(ev_compile_flags "${ev_compile_flags} -Wno-parentheses")
#    endif()
    set(ev_compile_flags "${ev_compile_flags} -DENABLE_BUNDLED_LIBEV=1")

    if (TARGET_OS_LINUX)
        #
        # Enable Linux-specific event notification API (man inotify)
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_INOTIFY")
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_EVENTFD")
        set(ev_compile_flags "${ev_compile_flags} -DEV_USE_SIGNALFD")
    elseif (TARGET_OS_FREEBSD OR TARGET_OS_DARWIN)
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

    message(STATUS "Use bundled libev includes: "
        "${LIBEV_INCLUDE_DIR}/tarantool_ev.h")
    message(STATUS "Use bundled libev library: "
        "${LIBEV_LIBRARIES}")

    unset(ev_src)
    unset(ev_compile_flags)
    unset(ev_link_libraries)
endmacro(libev_build)
