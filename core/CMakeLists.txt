#
# libev library
#
add_library(ev tarantool_ev.c)

set_source_files_properties(tarantool_ev.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused-result")

if (TARGET_OS_LINUX)
#
# Enable Linux-specific event notification API (man inotify)
  set_target_properties(ev PROPERTIES COMPILE_FLAGS "-DEV_USE_INOTIFY")
elseif (TARGET_OS_FREEBSD)
#
# On FreeBSD build libev loop on top of
  set_target_properties(ev PROPERTIES COMPILE_FLAGS "-DEV_USE_KQUEUE")
endif()
#
# libev uses ceil and floor from the standard math library
#
target_link_libraries(ev m)

#
# Build admin.c from admin.rl, but only if admin.rl was changed.
# The same applies to memcached.c/memcached.rl.
# We track admin.c and memcached.c in revision control, and thus do not
# require engineers who do not modify .rl files to have Ragel
# installed.
#
add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/core/admin.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 core/admin.rl -o core/admin.c
    DEPENDS ${CMAKE_SOURCE_DIR}/core/admin.rl)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 mod/silverbox/memcached.rl -o mod/silverbox/memcached.c
    DEPENDS ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.rl)
#
# Optionally rebuild the configuration file parsing code from
# templates.
#
macro(generate_config module)
# Gracefully handle out-of-source builds with missing 
# 'confetti'. Make sure that generated files are older than
# their sources when configuring the project.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.h
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.c
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.cfg)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.h
           ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.c
           ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.cfg
    COMMAND ${ECHO} '%{' > tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/tarantool_cfg.h >> tmp.cfg
    COMMAND ${ECHO} '\#include \"cfg/tarantool_${module}_cfg.h\"' >> tmp.cfg
    COMMAND ${ECHO} '%}' >> tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl >> tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/mod/${module}/${module}_cfg.cfg_tmpl >>
            tmp.cfg
    COMMAND ${CONFETTI} -i tmp.cfg -n tarantool_cfg
            -c ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.c
            -h ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.h
            -f ${CMAKE_SOURCE_DIR}/cfg/tarantool_${module}_cfg.cfg
    COMMAND ${CMAKE_COMMAND} -E remove tmp.cfg
    DEPENDS ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl
            ${CMAKE_SOURCE_DIR}/cfg/tarantool_cfg.h
            ${CMAKE_SOURCE_DIR}/mod/${module}/${module}_cfg.cfg_tmpl
    )
endmacro()

generate_config("silverbox")
generate_config("feeder")

execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.h
    ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.h
           ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c
    COMMAND ${ECHO} '%{' > tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/tarantool_cfg.h >> tmp.cfg
    COMMAND ${ECHO} '%}' >> tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl >> tmp.cfg
    COMMAND ${CONFETTI} -i tmp.cfg -n tarantool_cfg
            -H ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.h
            -p ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c
    COMMAND ${CMAKE_COMMAND} -E remove tmp.cfg
    DEPENDS ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl
            ${CMAKE_SOURCE_DIR}/cfg/tarantool_cfg.h
    )
#
# Make sure all generated headers are built before they are used.
#
add_custom_target(generate_headers
    DEPENDS ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c
            ${CMAKE_SOURCE_DIR}/cfg/tarantool_silverbox_cfg.h
            ${CMAKE_SOURCE_DIR}/cfg/tarantool_feeder_cfg.h)
# Do not list this helper target among all high-level targets.
set_target_properties(generate_headers PROPERTIES EXCLUDE_FROM_ALL TRUE)

#
# Do not clean admin.c, memcached.c or other
# generated files in 'make clean' -- they are under
# revision control.
#
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

set (recompiled_sources tarantool.c say.c admin.c fiber.c)

add_library(ltfeeder STATIC ${recompiled_sources})
set_target_properties(ltfeeder
    PROPERTIES COMPILE_FLAGS
    "${core_cflags} -DUTILITY -DTARANTOOL_CONFIG='<cfg/tarantool_feeder_cfg.h>'")
add_dependencies(ltfeeder generate_headers)

add_library(ltbox STATIC ${recompiled_sources})
set_target_properties(ltbox
    PROPERTIES COMPILE_FLAGS
    "${core_cflags} -DSTORAGE -DTARANTOOL_CONFIG='<cfg/tarantool_silverbox_cfg.h>'")
add_dependencies(ltbox generate_headers)

set (common_sources tbuf.c palloc.c util.c
    salloc.c pickle.c coro.c stat.c log_io.c
    ${CMAKE_SOURCE_DIR}/cfg/warning.c
    ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c
    )
add_library(ltcommon STATIC ${common_sources})

set (common_libraries ltcommon ev coro gopt misc)

if (ENABLE_GCOV)
  set (common_libraries ${common_libraries} gcov)
endif()

if (ENABLE_BACKTRACE AND HAVE_BFD)
  set (common_libraries ${common_libraries} bfd)
endif()

set (silverbox_sources
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_silverbox_cfg.c
    ${CMAKE_SOURCE_DIR}/mod/silverbox/index.c
    ${CMAKE_SOURCE_DIR}/mod/silverbox/box.c
    ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.c
    log_io_remote.c iproto.c)

if (ENABLE_TRACE)
  set (silverbox_sources ${silverbox_sources} trace.c)
endif()

set (feeder_sources
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_feeder_cfg.c
    ${CMAKE_SOURCE_DIR}/mod/feeder/feeder.c)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.c
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_silverbox_cfg.c
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_feeder_cfg.c
    ${CMAKE_SOURCE_DIR}/third_party/confetti/prscfg.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused")

add_executable(tarantool_silverbox ${silverbox_sources})
target_link_libraries(tarantool_silverbox ltbox ${common_libraries})

add_executable(tarantool_feeder ${feeder_sources})
target_link_libraries(tarantool_feeder ltfeeder ${common_libraries})

set_target_properties(tarantool_silverbox tarantool_feeder ltcommon
    PROPERTIES COMPILE_FLAGS "${core_cflags}")
