find_program(DTRACE dtrace)

if(DTRACE)
    set(DTRACE_FOUND ON)
endif(DTRACE)

if(DTRACE_FOUND AND ENABLE_DTRACE)
    set(DTRACE_OBJS)
    message(STATUS "DTrace found")
    set(DTRACE_O_DIR ${CMAKE_CURRENT_BINARY_DIR}/dtrace)
    execute_process(COMMAND mkdir ${DTRACE_O_DIR})
    message(STATUS "DTrace obj dir ${DTRACE_O_DIR}")
else(DTRACE_FOUND AND ENABLE_DTRACE)
    message(FATAL_ERROR "Could not find DTrace")
endif (DTRACE_FOUND AND ENABLE_DTRACE)

macro(dtrace_gen_h provider header)
    message(STATUS "DTrace generate ${header}")
    execute_process(
        COMMAND ${DTRACE} -h -s ${provider} -o ${header}
    )
endmacro(dtrace_gen_h)
