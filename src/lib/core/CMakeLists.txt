set(core_sources
    diag.c
    crash.c
    say.c
    memory.c
    clock.c
    fiber.c
    backtrace.cc
    cbus.c
    fiber_pool.c
    fiber_cond.c
    fiber_channel.c
    latch.c
    sio.c
    evio.c
    coio.cc
    coio_task.c
    coio_file.c
    popen.c
    coio_buf.cc
    fio.c
    exception.cc
    errinj.c
    reflection.c
    assoc.c
    util.c
    random.c
    trigger.cc
    port.c
    decimal.c
    mp_decimal.c
    cord_buf.c
)

if (TARGET_OS_NETBSD)
    # A workaround for "undefined reference to `__gcc_personality_v0'"
    # on x86_64-rumprun-netbsd-gcc
    set_source_files_properties(util.c PROPERTIES COMPILE_FLAGS
        -fno-exceptions)
endif()

add_library(core STATIC ${core_sources})

target_link_libraries(core salad small uri decNumber bit ${LIBEV_LIBRARIES}
                      ${LIBEIO_LIBRARIES} ${LIBCORO_LIBRARIES}
                      ${MSGPUCK_LIBRARIES} ${ICU_LIBRARIES})

if (ENABLE_BACKTRACE AND NOT TARGET_OS_DARWIN)
    target_link_libraries(core gcc_s ${UNWIND_LIBRARIES})
endif()

# Since fiber.top() introduction, fiber.cc, which is part of core
# library, depends on clock_gettime() syscall, so we should set
# -lrt when it is appropriate. See a comment for
# HAVE_CLOCK_GETTIME_WITHOUT_RT in ${REPO}/CMakeLists.txt.
if ("${HAVE_CLOCK_GETTIME}" AND NOT "${HAVE_CLOCK_GETTIME_WITHOUT_RT}")
    target_link_libraries(core rt)
endif()
