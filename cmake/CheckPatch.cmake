find_program(CHECKPATCH checkpatch.pl
             HINTS ${PROJECT_SOURCE_DIR}/checkpatch)
find_program(CODESPELL codespell)

add_custom_target(checkpatch)

set(BASE_GIT_REF "origin/master")
if(DEFINED ENV{CHECKPATCH_GITREF})
    set(BASE_GIT_REF "$ENV{CHECKPATCH_GITREF}")
endif()

# Description of supported checks in
# https://github.com/tarantool/checkpatch/blob/master/doc/checkpatch.rst
set(CHECKPATCH_OPTIONS --git ${BASE_GIT_REF}..HEAD --show-types)
if(CODESPELL)
    set(CHECKPATCH_OPTIONS ${CHECKPATCH_OPTIONS} --codespell)
endif(CODESPELL)

if(CHECKPATCH)
    add_custom_command(TARGET checkpatch
        COMMENT "Running checkpatch on a current branch against ${BASE_GIT_REF}"
        COMMAND ${CHECKPATCH} ${CHECKPATCH_OPTIONS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
else()
    set(WARN_MSG "`checkpatch.pl' is not found, so checkpatch target is dummy.")
    add_custom_command(TARGET ${PROJECT_NAME}-checkpatch
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${WARN_MSG}
        COMMENT ${WARN_MSG}
    )
endif(CHECKPATCH)
