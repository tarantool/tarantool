
if (JING STREQUAL "JING-NOTFOUND")
    message (FATAL_ERROR "jing is missing")
endif()

set(PATH_DEVGUIDE_HTML "${PROJECT_BINARY_DIR}/doc/www-data/tarantool_developer_guide.html")
set(PATH_DEVGUIDE_TXT "${PROJECT_BINARY_DIR}/doc/developer/tarantool_developer_guide.txt")

add_custom_target(doc-check-dev ALL
    COMMAND ${JING} http://docbook.org/xml/5.0/rng/docbookxi.rng
            ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_command(OUTPUT ${PATH_DEVGUIDE_HTML}
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o ${PATH_DEVGUIDE_HTML}
        ${CMAKE_SOURCE_DIR}/doc/user/tnt-html.xsl
        ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_command(OUTPUT ${PATH_DEVGUIDE_TXT}
    DEPENDS ${PATH_DEVGUIDE_HTML}
    COMMAND ${LYNX} -dump ${PATH_DEVGUIDE_HTML} > ${PATH_DEVGUIDE_TXT})

add_custom_target(dev-html DEPENDS ${PATH_DEVGUIDE_HTML})
add_custom_target(dev-txt  DEPENDS ${PATH_DEVGUIDE_TXT})

add_custom_target(doc-autogen-dev ALL
    DEPENDS ${PATH_DEVGUIDE_HTML} ${PATH_DEVGUIDE_TXT})

if ("${CPACK_GENERATOR}" STREQUAL "RPM")
    install (FILES ${PATH_DEVGUIDE_HTML}
                   ${PATH_DEVGUIDE_TXT} DESTINATION /usr/local/doc/tarantool)
else()
    install (FILES ${PATH_DEVGUIDE_HTML}
                   ${PATH_DEVGUIDE_TXT} DESTINATION share/doc/tarantool)
endif()
