if (JING STREQUAL "JING-NOTFOUND")
    message (FATAL_ERROR "jing is missing")
endif()

set(PATH_DEVGUIDE_HTML "${PROJECT_BINARY_DIR}/doc/www/content/docs/dev_guide.html")
set(PATH_DEVGUIDE_HTML_CSS "${PROJECT_BINARY_DIR}/doc/www/content/docs/")
set(PATH_DEVGUIDE_TXT "${PROJECT_BINARY_DIR}/doc/developer/dev_guide.txt")

add_custom_target(doc-check-dev
    COMMAND ${JING} http://docbook.org/xml/5.0/rng/docbookxi.rng
            ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_command(OUTPUT ${PATH_DEVGUIDE_HTML}
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o ${PATH_DEVGUIDE_HTML} tnt-html.xsl
        ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_target(html-dev-saxon
    COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-resolver.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar:/usr/share/java/xml-commons-resolver-1.1.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u -o ${PATH_DEVGUIDE_HTML} -w1 developer.xml tnt-html.xsl
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH_DEVGUIDE_HTML_CSS}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/doc/developer/tnt.css ${PATH_DEVGUIDE_HTML_CSS})


add_custom_command(OUTPUT ${PATH_DEVGUIDE_TXT}
    DEPENDS html-dev-saxon
    COMMAND ${LYNX} -dump ${PATH_DEVGUIDE_HTML} > ${PATH_DEVGUIDE_TXT})

add_custom_target(dev-html DEPENDS html-dev-saxon)
add_custom_target(dev-txt  DEPENDS ${PATH_DEVGUIDE_TXT})

add_custom_target(doc-autogen-dev ALL
    DEPENDS dev-html dev-txt)

install (FILES ${PATH_DEVGUIDE_HTML}
               ${PATH_DEVGUIDE_TXT} DESTINATION share/doc/tarantool)
