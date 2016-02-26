# Generate man pages of the project by using the POD header
# written in the tool source code. To use it - include this
# file in CMakeLists.txt and invoke
# pod2man(<podfile> <manfile> <section> <center>)
find_program(POD2MAN pod2man)

if(NOT POD2MAN)
    message(STATUS "Could not find pod2man - man pages disabled")
endif(NOT POD2MAN)

macro(pod2man PODFILE NAME SECTION CENTER)
    set(PODFILE_FULL "${CMAKE_CURRENT_SOURCE_DIR}/${PODFILE}")
    set(MANFILE_FULL "${CMAKE_CURRENT_BINARY_DIR}/${NAME}.${SECTION}")
    if(NOT EXISTS ${PODFILE_FULL})
        set(PODFILE_FULL "${CMAKE_CURRENT_BINARY_DIR}/${PODFILE}")
    endif(NOT EXISTS ${PODFILE_FULL})
    if(NOT EXISTS ${PODFILE_FULL})
        message(FATAL ERROR "Could not find pod file ${PODFILE_FULL} to generate man page")
    endif(NOT EXISTS ${PODFILE_FULL})

    if(POD2MAN)
        add_custom_command(
            OUTPUT ${MANFILE_FULL}
            COMMAND ${POD2MAN} --section="${SECTION}" --center="${CENTER}"
                --release --name="${NAME}" "${PODFILE_FULL}" "${MANFILE_FULL}"
        )
        set(MANPAGE_TARGET "man-${NAME}")
        add_custom_target(${MANPAGE_TARGET} ALL
            DEPENDS ${MANFILE_FULL}
        )
        install(
            FILES ${MANFILE_FULL}
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man${SECTION}
        )
    endif()
endmacro(pod2man PODFILE NAME SECTION OUTPATH CENTER)
