#
# Doing it in a function to avoid polluting the toplevel namespace
function (do_pthread_checks)
    check_include_file(pthread_np.h HAVE_PTHREAD_NP_H)
    if (HAVE_PTHREAD_NP_H)
        set(INCLUDE_MISC_PTHREAD_HEADERS "#include <pthread_np.h>")
    endif ()
    set(CMAKE_REQUIRED_FLAGS -pedantic-errors)
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE -D_DARWIN_C_SOURCE)
    set(CMAKE_REQUIRED_LIBRARIES pthread)
    # pthread_setname_np(<thread_id>, <name>) - Linux
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { pthread_setname_np(pthread_self(), \"\"); }
        " HAVE_PTHREAD_SETNAME_NP)
    # pthread_setname_np(<name>) - OSX
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { pthread_setname_np(\"\"); }
        " HAVE_PTHREAD_SETNAME_NP_1)
    # pthread_set_name_np(<thread_id>, <name>) - *BSD
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { pthread_set_name_np(pthread_self(), \"\"); }
        " HAVE_PTHREAD_SET_NAME_NP)
    if (NOT (HAVE_PTHREAD_SETNAME_NP OR
             HAVE_PTHREAD_SETNAME_NP_1 OR
             HAVE_PTHREAD_SET_NAME_NP))
        message(FATAL_ERROR "No suitable function for setting thread names found")
    endif ()
endfunction (do_pthread_checks)
do_pthread_checks()

