# Compares the options in the CMake cache of an external project with the
# options that are passed to the project or excluded explicitly, like
# `diff -u <(sort passed excluded) <(sort present)` does:
#
# --- <OPTIONS_FILE>: the file that passes and excludes the options;
# +++ <CACHE_FILE>: the CMake cache of the project;
# +<option>: present in the cache, but neither passed nor excluded, for
#            example, a new option after an update of the project;
# -<option>: passed or excluded, but absent in the cache, for example, a
#            removed option or an option that is listed twice.
#
# The options matching one of the IGNORED_OPTIONS regular expressions are
# not compared.
#
# The cache file is parsed, because `cmake -LA <build dir>` reconfigures
# the project.

if(NOT CACHE_FILE OR NOT OPTIONS_FILE)
    message(FATAL_ERROR "Usage: ${CMAKE_COMMAND} -DCACHE_FILE=<FILENAME> "
        "-DOPTIONS_FILE=<FILENAME> -DPASSED_OPTIONS=<LIST> "
        "-DEXCLUDED_OPTIONS=<LIST> -DIGNORED_OPTIONS=<LIST> "
        "-P CheckExternalProjectOptions.cmake")
elseif(NOT EXISTS "${CACHE_FILE}")
    message(FATAL_ERROR "${CACHE_FILE}: No such file")
endif()

function(is_ignored option result)
    foreach(pattern IN LISTS IGNORED_OPTIONS)
        if(option MATCHES "^(${pattern})$")
            set(${result} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${result} FALSE PARENT_SCOPE)
endfunction()

# `cmake -LA` lists the entries of all types except INTERNAL, STATIC and
# UNINITIALIZED. A -D value that the project doesn't declare stays
# UNINITIALIZED.
file(STRINGS "${CACHE_FILE}" entries
     REGEX "^[^#/][^=:]*:(BOOL|PATH|FILEPATH|STRING)=")
set(present "")
foreach(entry IN LISTS entries)
    string(REGEX REPLACE ":.*" "" option "${entry}")
    is_ignored(${option} ignored)
    if(NOT ignored)
        list(APPEND present ${option})
    endif()
endforeach()

set(diff "")
foreach(option IN LISTS PASSED_OPTIONS EXCLUDED_OPTIONS)
    is_ignored(${option} ignored)
    if(ignored)
        continue()
    endif()
    list(FIND present ${option} index)
    if(index EQUAL -1)
        list(APPEND diff "${option} -")
    else()
        list(REMOVE_AT present ${index})
    endif()
endforeach()
foreach(option IN LISTS present)
    list(APPEND diff "${option} +")
endforeach()

if(diff)
    list(SORT diff)
    string(REGEX REPLACE "([^;]*) ([-+])" "\\2\\1" diff "${diff}")
    string(REPLACE ";" "\n  " diff "${diff}")
    # The indented lines are printed as they are, not reflowed.
    message(FATAL_ERROR "  --- ${OPTIONS_FILE}\n"
        "  +++ ${CACHE_FILE}\n  ${diff}")
endif()
