# Generate man pages of the project by using the POD header
# written in the tool source code. To use it - include this
# file in CMakeLists.txt and invoke
# pod2man(<podfile> <manfile> <section> <center>)
find_program(POD2MAN pod2man)

if(NOT POD2MAN)
    message(STATUS "Could not find pod2man - man pages disabled")
endif(NOT POD2MAN)

macro(pod2man PODFILE MANFILE SECTION OUTPATH CENTER)
    if(NOT EXISTS ${PODFILE})
        message(FATAL ERROR "Could not find pod file ${PODFILE} to generate man page")
    endif(NOT EXISTS ${PODFILE})

    if(POD2MAN)
        set(OUTPATH_NEW "${PROJECT_BINARY_DIR}/${OUTPATH}")

        add_custom_command(
            OUTPUT ${OUTPATH_NEW}/${MANFILE}.${SECTION}
            COMMAND ${POD2MAN} --section ${SECTION} --center ${CENTER}
                --release --name ${MANFILE} ${PODFILE}
                ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        )
        set(MANPAGE_TARGET "man-${MANFILE}")
        add_custom_target(${MANPAGE_TARGET} ALL
            DEPENDS ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        )
        install(
            FILES ${OUTPATH_NEW}/${MANFILE}.${SECTION}
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man${SECTION}
        )
    endif()
endmacro(pod2man PODFILE MANFILE SECTION OUTPATH CENTER)
