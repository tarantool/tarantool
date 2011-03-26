add_custom_command(OUTPUT tarantool_user_guide.html
    COMMAND ${XSLTPROC} --xinclude -o tarantool_user_guide.html
        http://docbook.sourceforge.net/release/xsl-ns/current/html/docbook.xsl
        user.xml)


