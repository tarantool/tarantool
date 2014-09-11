find_program(PELICAN pelican)

if (PELICAN STREQUAL "PELICAN-NOTFOUND")
    message (FATAL_ERROR "pelican is missing")
endif()

add_custom_target(www ALL
    COMMAND ${PELICAN} -v)
