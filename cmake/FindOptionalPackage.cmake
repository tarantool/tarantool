if (NOT _OptionalPackagesFile)
    set(_OptionalPackagesFile
        ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/OptionalPackages.txt)
    if (EXISTS ${_OptionalPackagesFile})
        file(REMOVE ${_OptionalPackagesFile})
    endif()
endif()

macro (find_optional_package _package)
    string(TOUPPER ${_package} _packageUpper)
    if (WITH_${_packageUpper})
        # WITH_${_packageUpper} option requested by the user
        set(${_packageUpper}_FIND_REQUIRED ON)
    endif()
    option(WITH_${_packageUpper} "Search for ${_package} package" ON)
    if (WITH_${_packageUpper})
        find_package(${_package} ${ARGN})
    else (WITH_${_packageUpper})
        set(${_package}_FOUND OFF)
        set(${_packageUpper}_FOUND OFF)
    endif ()
    file(APPEND "${_OptionalPackagesFile}"
         "-- WITH_${_packageUpper}=${WITH_${_packageUpper}}\n")
endmacro (find_optional_package)

macro(list_optional_packages)
    file(READ ${_OptionalPackagesFile} _message)
    message(STATUS "\n${_message}")
endmacro()
