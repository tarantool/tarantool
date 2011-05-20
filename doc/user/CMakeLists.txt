add_custom_target(html ALL
    COMMAND ${XSLTPROC}
        --stringparam collect.xref.targets "all"
        --xinclude -o tarantool_user_guide.html
        tnt-html.xsl
        user.xml)
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
