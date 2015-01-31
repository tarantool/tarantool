set(PATH_DEVGUIDE_HTML "${PROJECT_BINARY_DIR}/doc/www/content/doc/dev_guide.html")
set(PATH_DEVGUIDE_TXT "${PROJECT_BINARY_DIR}/doc/developer/dev_guide.txt")

add_custom_target(dev-doc-check
        COMMAND ${JING} http://docbook.org/xml/5.0/rng/docbookxi.rng
        ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_target(dev-html
        COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o ${PATH_DEVGUIDE_HTML} tnt-html.xsl
        ${CMAKE_SOURCE_DIR}/doc/developer/developer.xml)

add_custom_target(dev-html-saxon
        COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-resolver.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar:/usr/share/java/xml-commons-resolver-1.1.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u -o ${PATH_DEVGUIDE_HTML} -w1 developer.xml tnt-html.xsl)


