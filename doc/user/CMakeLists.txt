set(PATH_USERGUIDE_HTML "${PROJECT_BINARY_DIR}/doc/www/content/docs/user_guide.html")
set(PATH_USERGUIDE_HTML_CSS "${PROJECT_BINARY_DIR}/doc/www/output/docs/")
set(PATH_USERGUIDE_HTML_CHUNK "${PROJECT_BINARY_DIR}/doc/www/content/docs/user_guide/")
set(PATH_USERGUIDE_HTML_CHUNK_CSS "${PROJECT_BINARY_DIR}/doc/www/output/docs/user_guide/")
set(PATH_USERGUIDE_TXT "${PROJECT_BINARY_DIR}/doc/user/tarantool_user_guide.txt")

if (XMLLINT STREQUAL "XMLLINT-NOTFOUND")
    message (FATAL_ERROR "xmllint is missing")
endif()

if (XSLTPROC STREQUAL "XSLTPROC-NOTFOUND")
    message (FATAL_ERROR "xsltproc is missing")
endif()

if (LYNX STREQUAL "LYNX-NOTFOUND")
    message (FATAL_ERROR "lynx is missing")
endif()

# XMLLINT is not able to validate SCHEMATRON constraints, and
# therefore is not a good validation tool for DocBook 5.  However,
# it can validate the entire document, following xinclude
# directives, and thus is used here. To validate individual XML
# files, or for troubleshooting, I (Kostja) recommend using jing,
# since its diagnostics output is significantly more readable:
# jing http://docbook.org/xml/5.0/rng/docbookxi.rng file.xml
#
add_custom_target(doc-check ALL
    COMMAND ${XMLLINT} --xinclude --noout --relaxng http://docbook.org/xml/5.0/rng/docbookxi.rng
            ${CMAKE_SOURCE_DIR}/doc/user/user.xml)

#
# xsltproc-based documentation generation (default)
#
add_custom_command(OUTPUT ${PATH_USERGUIDE_HTML_CHUNK}
    COMMAND ${XSLTPROC} --nonet --xinclude
        --stringparam base.dir "${PATH_USERGUIDE_HTML_CHUNK}"
        ${CMAKE_SOURCE_DIR}/doc/user/tnt-html-chunk.xsl
        ${CMAKE_SOURCE_DIR}/doc/user/user.xml)

add_custom_command(OUTPUT ${PATH_USERGUIDE_HTML}
    COMMAND ${XSLTPROC} --nonet --xinclude
        -o ${PATH_USERGUIDE_HTML}
        ${CMAKE_SOURCE_DIR}/doc/user/tnt-html.xsl
        ${CMAKE_SOURCE_DIR}/doc/user/user.xml)

add_custom_command(OUTPUT ${PATH_USERGUIDE_TXT}
    DEPENDS ${PATH_USERGUIDE_HTML}
    COMMAND ${LYNX} -dump ${PATH_USERGUIDE_HTML} > ${PATH_USERGUIDE_TXT})

add_custom_target(html DEPENDS ${PATH_USERGUIDE_HTML})
add_custom_target(html-chunk DEPENDS ${PATH_USERGUIDE_HTML_CHUNK})
add_custom_target(txt  DEPENDS ${PATH_USERGUIDE_TXT})

add_custom_target(doc-autogen ALL
    DEPENDS html-user-saxon html-user-saxon-chunk)

install (FILES ${PATH_USERGUIDE_HTML}
               ${PATH_USERGUIDE_TXT} DESTINATION share/doc/tarantool)

install (DIRECTORY ${PATH_USERGUIDE_HTML_CHUNK} DESTINATION share/doc/tarantool)

add_custom_target(pdf
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o tarantool_user_guide.fo
        ${CMAKE_SOURCE_DIR}/doc/user/tnt-fo.xsl
        ${CMAKE_SOURCE_DIR}/doc/user/user.xml
    COMMAND fop tarantool_user_guide.fo tarantool_user_guide.pdf)

add_custom_target(relink
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "only"
        --xinclude tnt-html.xsl user.xml)

#
# Java saxon-based documentation generation (misc)
#
add_custom_target(html-user-saxon
    COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-resolver.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar:/usr/share/java/xml-commons-resolver-1.1.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u -o ${PATH_USERGUIDE_HTML} user.xml tnt-html.xsl
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH_USERGUIDE_HTML_CSS}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/doc/user/tnt.css ${PATH_USERGUIDE_HTML_CSS})

add_custom_target(html-user-saxon-chunk
    COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-resolver.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar:/usr/share/java/xml-commons-resolver-1.1.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u user.xml tnt-html-chunk.xsl
        base.dir="${PATH_USERGUIDE_HTML_CHUNK}"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PATH_USERGUIDE_HTML_CHUNK_CSS}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/doc/user/tnt.css ${PATH_USERGUIDE_HTML_CHUNK_CSS})


add_custom_target(pdf-saxon
    COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-commons-resolver-1.1.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u -o tarantool_user_guide.fo user.xml tnt-fo.xsl
    COMMAND fop tarantool_user_guide.fo tarantool_user_guide.pdf)
