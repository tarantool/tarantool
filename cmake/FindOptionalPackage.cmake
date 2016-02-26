if (NOT _OptionalPackagesFile)
    set(_OptionalPackagesFile
        ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/OptionalPackages.txt)
    if (EXISTS ${_OptionalPackagesFile})
        file(REMOVE ${_OptionalPackagesFile})
    endif()
endif()
file(APPEND "${_OptionalPackagesFile}" "")

macro (find_optional_package _package)
    string(TOUPPER ${_package} _packageUpper)
    if (NOT DEFINED WITH_${_packageUpper})
        # First run and WITH_${_packageUpper} option is not set by the user.
        # Enable auto-mode and try to find package.
        find_package(${_package} ${ARGN})
    elseif (WITH_${_packageUpper})
        # Non-first run or WITH_${_packageUpper} was set by the user.
        # Force error if the package will not be found.
        set(${_packageUpper}_FIND_REQUIRED ON)
        find_package(${_package} ${ARGN})
    endif ()
    if (${_package}_FOUND OR ${_packageUpper}_FOUND)
        set(_default ON)
    else()
        set(_default OFF)
    endif()
    # Add the user option and (!) update the cache
    option(WITH_${_packageUpper} "Search for ${_package} package" ${_default})
    # Now ${WITH_${_packageUpper}} is either ON or OFF
    file(APPEND "${_OptionalPackagesFile}"
         "-- WITH_${_packageUpper}=${WITH_${_packageUpper}}\n")
endmacro (find_optional_package)

macro(list_optional_packages)
    file(READ ${_OptionalPackagesFile} _message)
    find_package_message(OPTIONAL_PACKAGES "${_message}" "${_message}")
endmacro()
