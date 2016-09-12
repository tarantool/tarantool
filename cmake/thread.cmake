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
    # pthread_getattr_np - Linux
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { pthread_attr_t a; pthread_getattr_np(pthread_self(), &a); }
        " HAVE_PTHREAD_GETATTR_NP)
    # pthread_get_stacksize_np - OSX
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { (void)pthread_get_stacksize_np(pthread_self()); }
        " HAVE_PTHREAD_GET_STACKSIZE_NP)
    # pthread_get_stackaddr_np - OSX
    check_c_source_compiles("
        #include <pthread.h>
        ${INCLUDE_MISC_PTHREAD_HEADERS}
        int main() { (void)pthread_get_stackaddr_np(pthread_self()); }
        " HAVE_PTHREAD_GET_STACKADDR_NP)
endfunction (do_pthread_checks)
do_pthread_checks()

