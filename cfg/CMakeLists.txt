
# Do not clean the generated configuration files in "make clean"
#
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/cfg/prscfg.h
    ${CMAKE_SOURCE_DIR}/cfg/prscfg.c)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/cfg/prscfg.h
           ${CMAKE_SOURCE_DIR}/cfg/prscfg.c
    COMMAND ${ECHO} '%{' > tmp.cfg
    COMMAND ${ECHO} '\#include \"cfg/warning.h\"' >> tmp.cfg
    COMMAND ${ECHO} '%}' >> tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl >> tmp.cfg
    COMMAND ${CONFETTI} -i tmp.cfg -n tarantool_cfg
            -H ${CMAKE_SOURCE_DIR}/cfg/prscfg.h
            -p ${CMAKE_SOURCE_DIR}/cfg/prscfg.c
    COMMAND ${CMAKE_COMMAND} -E remove tmp.cfg
    DEPENDS ${CMAKE_SOURCE_DIR}/cfg/warning.h)

#
# Optionally rebuild the configuration file parsing code from
# templates.
#
macro(generate_mod_config mod)
# Gracefully handle out-of-source builds with missing 
# 'confetti'. Make sure that generated files are older than
# their sources when configuring the project.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h
           ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c
    COMMAND ${ECHO} '%{' > ${mod}_tmp.cfg
    COMMAND ${ECHO} '\#include \"cfg/warning.h\"' >> ${mod}_tmp.cfg
    COMMAND ${ECHO} '\#include \"cfg/tarantool_${mod}_cfg.h\"' >> ${mod}_tmp.cfg
    COMMAND ${ECHO} '%}' >> ${mod}_tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl >> ${mod}_tmp.cfg
    COMMAND ${CAT} ${CMAKE_SOURCE_DIR}/mod/${mod}/${mod}_cfg.cfg_tmpl >>
            ${mod}_tmp.cfg
    COMMAND ${CONFETTI} -i ${mod}_tmp.cfg -n tarantool_cfg
            -c ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c
            -h ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h
    COMMAND ${CMAKE_COMMAND} -E remove ${mod}_tmp.cfg
    DEPENDS ${CMAKE_SOURCE_DIR}/cfg/core_cfg.cfg_tmpl
            ${CMAKE_SOURCE_DIR}/cfg/warning.h
            ${CMAKE_SOURCE_DIR}/mod/${mod}/${mod}_cfg.cfg_tmpl
    )
endmacro()

foreach (mod ${TARANTOOL_MODULES})
  generate_mod_config(${mod})
  set (generated_headers ${generated_headers} 
       ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h)
endforeach()

set_source_files_properties(${CMAKE_SOURCE_DIR}/cfg/prscfg.c
    PROPERTIES COMPILE_FLAGS "-Wno-unused")

add_library(cfg prscfg.c warning.c)
#
# Make sure all generated headers are built before they are used.
# This is also necessary to avoid races in parallel make,
# when tmp.cfg is corrupted by two paralled targets.
# See http://www.cmake.org/Bug/view.php?id=10082 for details.
#
add_custom_target(config
    DEPENDS ${CMAKE_SOURCE_DIR}/cfg/prscfg.h ${generated_headers})
