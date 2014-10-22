# Generate man pages of the project by using the POD header
# written in the tool source code. To use it - include this
# file in CMakeLists.txt and invoke
# POD2MAN(<podfile> <manfile> <section> <center>)
FIND_PROGRAM(POD2MAN pod2man)

MACRO(pod2man PODFILE MANFILE SECTION OUTPATH CENTER)
    if (NOT POD2MAN STREQUAL "POD2MAN-NOTFOUND")
        IF(NOT EXISTS ${PODFILE})
            message(FATAL ERROR "Could not find pod file ${PODFILE} to generate man page")
        ENDIF(NOT EXISTS ${PODFILE})

        SET(OUTPATH_NEW "${PROJECT_BINARY_DIR}/${OUTPATH}")

        ADD_CUSTOM_COMMAND(
            OUTPUT ${OUTPATH_NEW}/${MANFILE}.${SECTION}
            COMMAND ${POD2MAN} --section ${SECTION} --center ${CENTER}
                --release --stderr --name ${MANFILE} ${PODFILE} >
                ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        )
        SET(MANPAGE_TARGET "man-${MANFILE}")
        ADD_CUSTOM_TARGET(${MANPAGE_TARGET} ALL
            DEPENDS ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        )
        INSTALL(
            FILES ${OUTPATH_NEW}/${MANFILE}.${SECTION}
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man${SECTION}
        )
    endif()
ENDMACRO(pod2man PODFILE MANFILE SECTION OUTPATH CENTER)
