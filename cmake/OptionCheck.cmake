# Adds the `optcheck-<name>` and `optcheck` targets to verify options we pass
# to a library's build script against the ones the library declares.
#
# add_option_check(<name>
#     SOURCE_DIR <dir>          the sub-project's sources
#     OPTIONS_FILE <file>       where we pass the options, for the report
#     FLAGS <-D...>             the flags our build configures it with
#     INCLUDES <module>...      the CMake modules it includes
#     PACKAGES <package>...     the packages it finds
#     [MODULE_PATH <dir>...]    where its own Find modules live
#     [IGNORED <name>...])      names we pass deliberately; not reported
#
# The target calls cmake/OptionCheck.lua. The algorithm is described in the
# script.

set(OPTION_CHECK_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/OptionCheck.lua)

function(add_option_check NAME)
    cmake_parse_arguments(OC "" "SOURCE_DIR;OPTIONS_FILE"
        "FLAGS;INCLUDES;PACKAGES;MODULE_PATH;IGNORED" ${ARGN})

    if(NOT TARGET optcheck)
        add_custom_target(optcheck)
    endif()
    add_custom_target(optcheck-${NAME}
        COMMAND ${OPTION_CHECK_SCRIPT}
            --name ${NAME}
            --cmake ${CMAKE_COMMAND}
            --generator ${CMAKE_GENERATOR}
            --source-dir ${OC_SOURCE_DIR}
            --options-file ${OC_OPTIONS_FILE}
            --work-dir ${PROJECT_BINARY_DIR}/build/optcheck/${NAME}
            --flags ${OC_FLAGS}
            --includes ${OC_INCLUDES}
            --packages ${OC_PACKAGES}
            --module-path ${OC_MODULE_PATH}
            --ignored ${OC_IGNORED}
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green
            "${NAME} options are in sync"
        COMMENT "Checking the options passed to ${NAME}"
        VERBATIM)
    add_dependencies(optcheck optcheck-${NAME})
endfunction()
