function(GetLibUnwindVersion _LIBUNWIND_VERSION)
    set(_LIBUNWIND_VERSION_HEADER "${LIBUNWIND_INCLUDE_DIR}/libunwind-common.h")
    if(LIBUNWIND_LIBRARY AND EXISTS ${_LIBUNWIND_VERSION_HEADER})
        file(READ ${_LIBUNWIND_VERSION_HEADER}
             _LIBUNWIND_VERSION_HEADER_CONTENTS)
        string(REGEX MATCH
               "#define UNW_VERSION_MAJOR[ \t]+([0-9]+)\n#define UNW_VERSION_MINOR[ \t]+([0-9]+)"
               _VERSION_REGEX "${_LIBUNWIND_VERSION_HEADER_CONTENTS}")
        set(${_LIBUNWIND_VERSION} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}"
            PARENT_SCOPE)
    endif()
endfunction()
