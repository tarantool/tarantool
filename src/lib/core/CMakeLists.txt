set(core_sources
    diag.c
    crash.c
    say.c
    memory.c
    clock.c
    fiber.c
    cxx_abi.cc
    backtrace.c
    cbus.c
    fiber_pool.c
    fiber_cond.c
    fiber_channel.c
    latch.c
    sio.c
    evio.c
    coio.c
    coio_task.c
    coio_file.c
    popen.c
    fio.c
    exception.cc
    errinj.c
    error_payload.c
    reflection.c
    assoc.c
    util.c
    random.c
    trigger.cc
    port.c
    decimal.c
    mp_decimal.c
    cord_buf.c
    datetime.c
    iostream.c
    tt_uuid.c
    mp_uuid.c
    mp_datetime.c
    mp_interval.c
    prbuf.c
    clock_lowres.c
    ssl_init.c
    tt_sigaction.c
    tt_strerror.c
    mp_util.c
    cord_on_demand.cc
    tweaks.c
    tt_sort.c
    event.c
)

if (ENABLE_BACKTRACE)
    list(APPEND core_sources  proc_name_cache.cc)
endif()

if(ENABLE_TUPLE_COMPRESSION)
    list(APPEND core_sources ${TUPLE_COMPRESSION_CORE_SOURCES})
else()
    list(APPEND core_sources  tt_compression.c)
endif()

if(ENABLE_SSL)
    list(APPEND core_sources ${SSL_SOURCES})
else()
    list(APPEND core_sources ssl.c ssl_error.cc)
endif()

include_directories(${OPENSSL_INCLUDE_DIR}
                    ${EXTRA_CORE_INCLUDE_DIRS})

if (TARGET_OS_NETBSD)
    # A workaround for "undefined reference to `__gcc_personality_v0'"
    # on x86_64-rumprun-netbsd-gcc
    set_source_files_properties(util.c PROPERTIES COMPILE_FLAGS
        -fno-exceptions)
endif()

add_library(core STATIC ${core_sources})

target_link_libraries(core salad small uri decNumber bit tzcode misc
                      ${LIBEV_LIBRARIES}
                      ${LIBEIO_LIBRARIES} ${LIBCORO_LIBRARIES}
                      ${MSGPUCK_LIBRARIES} ${ICU_LIBRARIES}
                      ${LIBCDT_LIBRARIES} ${OPENSSL_LIBRARIES}
                      ${EXTRA_CORE_LINK_LIBRARIES})

if (ENABLE_BACKTRACE)
    target_link_libraries(core ${LIBUNWIND_LIBRARIES})
    if(ENABLE_BUNDLED_LIBUNWIND)
        add_dependencies(core bundled-libunwind bundled-libunwind-platform)
    endif()
endif()

if (ENABLE_TUPLE_COMPRESSION)
    target_link_libraries(core ${ZSTD_LIBRARIES})
endif()

# Since fiber.top() introduction, fiber.cc, which is part of core
# library, depends on clock_gettime() syscall, so we should set
# -lrt when it is appropriate. See a comment for
# HAVE_CLOCK_GETTIME_WITHOUT_RT in ${REPO}/CMakeLists.txt.
if ("${HAVE_CLOCK_GETTIME}" AND NOT "${HAVE_CLOCK_GETTIME_WITHOUT_RT}")
    target_link_libraries(core rt)
endif()
