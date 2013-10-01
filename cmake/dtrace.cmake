find_program(DTRACE dtrace)

macro(dtrace_gen_h provider header)
    message(STATUS "DTrace generate ${header}")
    execute_process(
        COMMAND ${DTRACE} -h -s ${provider} -o ${header}
    )
endmacro(dtrace_gen_h)

macro(dtrace_do_lib prefix orig_obj dir_to_objs)

        set(${prefix}_obj ${CMAKE_CURRENT_BINARY_DIR}/${dir_to_objs}/${orig_obj})
        set(dtrace_object ${DTRACE_O_DIR}/${prefix}_dtrace.o)

        add_custom_command(TARGET ${prefix}
            PRE_LINK
            COMMAND cp ${${prefix}_obj} ${DTRACE_O_DIR}/
            COMMAND ${DTRACE} -G -s ${DTRACE_D_FILE} -o ${dtrace_object} ${${prefix}_obj} 
        )
        set(DTRACE_OBJS ${DTRACE_OBJS} ${DTRACE_O_DIR}/${orig_obj})
        set(tmp_objs ${dtrace_object} ${${prefix}_obj})

        foreach(tmp_o in ${tmp_objs})
            set_source_files_properties(${tmp_o}
                PROPERTIES
                EXTERNAL_OBJECT true
                GENERATED true
            )
        endforeach(tmp_o)

        add_library(${prefix}_dtrace STATIC ${${prefix}_obj} ${dtrace_object})
        set_target_properties(${prefix}_dtrace PROPERTIES LINKER_LANGUAGE C)
endmacro(dtrace_do_lib)

if(DTRACE)
    set(DTRACE_FOUND ON)
endif(DTRACE)

if(DTRACE_FOUND AND ENABLE_DTRACE)
    set(DTRACE_OBJS)
    message(STATUS "DTrace found")
    set(DTRACE_O_DIR ${CMAKE_CURRENT_BINARY_DIR}/dtrace)
    set(DTRACE_D_FILE ${PROJECT_SOURCE_DIR}/include/dtrace.d)
    execute_process(COMMAND mkdir ${DTRACE_O_DIR})
    message(STATUS "DTrace obj dir ${DTRACE_O_DIR}")
    dtrace_gen_h(${DTRACE_D_FILE} ${PROJECT_SOURCE_DIR}/include/dtrace.h)
else(DTRACE_FOUND AND ENABLE_DTRACE)
    message(FATAL_ERROR "Could not find DTrace")
endif (DTRACE_FOUND AND ENABLE_DTRACE)

