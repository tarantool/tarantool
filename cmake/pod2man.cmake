# Generate man pages of the project by using the POD header
# written in the tool source code. To use it - include this
# file in CMakeLists.txt and invoke
# POD2MAN(<podfile> <manfile> <section> <center>)

FIND_PROGRAM(POD2MAN pod2man)

MACRO(POD2MAN PODFILE MANFILE SECTION OUTPATH CENTER)
    IF(NOT POD2MAN)
        MESSAGE(FATAL ERROR "Need pod2man installed to generate man page")
    ENDIF(NOT POD2MAN)

    IF(NOT EXISTS ${PODFILE})
        MESSAGE(FATAL ERROR "Could not find pod file ${PODFILE} to generate man page")
    ENDIF(NOT EXISTS ${PODFILE})

    SET(OUTPATH_NEW "${PROJECT_BINARY_DIR}/${OUTPATH}")

    ADD_CUSTOM_COMMAND(
        OUTPUT ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        COMMAND ${POD2MAN} --section ${SECTION} --center ${CENTER} --release
            --stderr --name ${MANFILE} ${PODFILE} > ${OUTPATH_NEW}/${MANFILE}.${SECTION}
    )

    SET(MANPAGE_TARGET "man-${MANFILE}")
    ADD_CUSTOM_TARGET(${MANPAGE_TARGET} DEPENDS ${OUTPATH_NEW}/${MANFILE}.${SECTION})
    ADD_DEPENDENCIES(man ${MANPAGE_TARGET})

    message(STATUS "${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}")
    message(STATUS "${MANFILE}.${SECTION} ${OUTPATH_NEW}/${MANFILE}.${SECTION}")
    INSTALL(
        FILES ${OUTPATH_NEW}/${MANFILE}.${SECTION}
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man${SECTION}
    )
ENDMACRO(POD2MAN PODFILE MANFILE SECTION)

MACRO(ADD_MANPAGE_TARGET)
    # It is not possible add a dependency to target 'install'
    # Run hard-coded 'make man' when 'make install' is invoked
    INSTALL(CODE "EXECUTE_PROCESS(COMMAND make man)")
    ADD_CUSTOM_TARGET(man)
ENDMACRO(ADD_MANPAGE_TARGET)
