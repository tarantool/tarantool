#
# A macro to build the bundled decNumber lisbrary.
macro(decnumber_build)
    set(decnumber_src
	${PROJECT_SOURCE_DIR}/third_party/decNumber/decNumber.c
	${PROJECT_SOURCE_DIR}/third_party/decNumber/decContext.c
	${PROJECT_SOURCE_DIR}/third_party/decNumber/decPacked.c
    )

    add_library(decNumber STATIC ${decnumber_src})

    set(DECNUMBER_INCLUDE_DIR ${PROJECT_BINARY_DIR}/third_party/decNumber)
    unset(decnumber_src)
endmacro(decnumber_build)
