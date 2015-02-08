find_program(PELICAN pelican)

if (PELICAN STREQUAL "PELICAN-NOTFOUND")
    message (FATAL_ERROR "python-pelican is missing")
endif()

add_custom_target(www ALL
    DEPENDS html-saxon html-saxon-chunk
    COMMAND ${PELICAN} -v)
