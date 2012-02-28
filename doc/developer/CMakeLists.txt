add_custom_target(dev-html ALL
    COMMAND ${XSLTPROC} --nonet
        --stringparam collect.xref.targets "all"
        --xinclude -o ${PROJECT_BINARY_DIR}/doc/www-data/tarantool_developer_guide.html
        ${CMAKE_SOURCE_DIR}/doc/user/tnt-html.xsl
        developer.xml)

add_custom_target(dev-check ALL
	COMMAND jing http://docbook.org/xml/5.0/rng/docbookxi.rng developer.xml
    )

