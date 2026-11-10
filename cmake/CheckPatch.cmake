find_program(CHECKPATCH checkpatch.pl
             HINTS ${PROJECT_SOURCE_DIR}/checkpatch)
find_program(CODESPELL codespell)
find_program(GIT git)

# Base Git reference to check the patches of the current branch against.
# Can be overridden with the CHECKPATCH_GITREF environment variable.
if(DEFINED ENV{CHECKPATCH_GITREF})
    set(BASE_GIT_REF "$ENV{CHECKPATCH_GITREF}")
elseif(GIT)
    # Prefer the upstream remote (tarantool/tarantool) over origin: origin is
    # usually a personal fork whose master can be far behind the actual base
    # of the branch, making the target check unrelated commits.
    foreach(remote upstream origin)
        execute_process(
            COMMAND ${GIT} symbolic-ref --short refs/remotes/${remote}/HEAD
            OUTPUT_VARIABLE remote_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE remote_head_result
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        )
        if(remote_head_result EQUAL 0 AND remote_head)
            set(BASE_GIT_REF "${remote_head}")
            break()
        endif()
    endforeach()
    if(NOT BASE_GIT_REF)
        set(BASE_GIT_REF "master")
    endif()
else()
    set(BASE_GIT_REF "master")
endif()

# Fail loudly instead of silently checking a bogus range if the base ref
# does not exist.
if(GIT)
    execute_process(
        COMMAND ${GIT} rev-parse --verify --quiet "${BASE_GIT_REF}"
        RESULT_VARIABLE base_ref_result
        OUTPUT_QUIET
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
    if(NOT base_ref_result EQUAL 0)
        message(WARNING "Checkpatch base Git reference '${BASE_GIT_REF}' is not found.")
    endif()
endif()

# Description of supported checks in
# https://github.com/tarantool/checkpatch/blob/master/doc/checkpatch.rst
set(CHECKPATCH_OPTIONS --git ${BASE_GIT_REF}..HEAD --show-types)
if(CODESPELL)
    set(CHECKPATCH_OPTIONS ${CHECKPATCH_OPTIONS} --codespell)
endif(CODESPELL)

if(CHECKPATCH)
    add_custom_target(checkpatch
        COMMENT "Running checkpatch on a current branch against ${BASE_GIT_REF}"
        COMMAND ${CHECKPATCH} ${CHECKPATCH_OPTIONS}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
else()
    set(WARN_MSG "`checkpatch.pl' is not found, so checkpatch target is dummy.")
    add_custom_target(checkpatch
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${WARN_MSG}
        COMMENT ${WARN_MSG}
    )
endif(CHECKPATCH)
