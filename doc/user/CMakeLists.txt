add_custom_target(html ALL
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o ${PROJECT_BINARY_DIR}/doc/www-data/tarantool_user_guide.html
        tnt-html.xsl
        user.xml)

add_custom_target(pdf
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o tarantool_user_guide.fo
        tnt-fo.xsl
        user.xml
    COMMAND fop tarantool_user_guide.fo tarantool_user_guide.pdf)

add_custom_target(relink
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "only"
        --xinclude -o ${PROJECT_BINARY_DIR}/doc/www-data/tarantool_user_guide.html
        tnt-html.xsl
        user.xml)

add_custom_target(html-saxon
    COMMAND java -cp "/usr/share/java/saxon.jar:/usr/share/java/xml-resolver.jar:/usr/share/java/docbook-xsl-saxon.jar:/usr/share/java/xercesImpl.jar:/etc/xml/resolver:/usr/share/java/xslthl.jar"
        -Djavax.xml.parsers.DocumentBuilderFactory=org.apache.xerces.jaxp.DocumentBuilderFactoryImpl
        -Djavax.xml.parsers.SAXParserFactory=org.apache.xerces.jaxp.SAXParserFactoryImpl
        -Dorg.apache.xerces.xni.parser.XMLParserConfiguration=org.apache.xerces.parsers.XIncludeParserConfiguration
        com.icl.saxon.StyleSheet
        -x org.apache.xml.resolver.tools.ResolvingXMLReader
        -y org.apache.xml.resolver.tools.ResolvingXMLReader
        -r org.apache.xml.resolver.tools.CatalogResolver
        -u -o ${PROJECT_BINARY_DIR}/doc/www-data/tarantool_user_guide.html user.xml tnt-html.xsl)

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

# XMLLINT is not able to validate SCHEMATRON constraints, and
# therefore is not a good validation tool for DocBook 5.  However,
# it can validate the entire document, following xinclude
# directives, and thus is used here. To validate individual XML
# files, or for troubleshooting, I (Kostja) recommend using jing,
# since its diagnostics output is significantly more readable:
# jing http://docbook.org/xml/5.0/rng/docbookxi.rng file.xml
#
add_custom_target(check ALL
	COMMAND ${XMLLINT} --xinclude --noout --relaxng http://docbook.org/xml/5.0/rng/docbookxi.rng user.xml
    )
