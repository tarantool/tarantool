add_library(tzcode STATIC
            strftime.c strptime.c timelocal.c localtime.c timezone.c)
target_link_libraries(tzcode)

set(TZCODE_PROJECT tzcode-iana)
set(TZCODE_BINARY_DIR ${PROJECT_BINARY_DIR}/build/${TZCODE_PROJECT})
set(TZCODE_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/tz)


set(TZDATA_TARGET main.zi)
set(TZDATA_FULL_TARGET ${TZCODE_SOURCE_DIR}/${TZDATA_TARGET})

set(TZCODE_SRC ${PROJECT_SOURCE_DIR}/src/lib/tzcode)
set(TZLUA_SRC ${PROJECT_SOURCE_DIR}/src/lua)

if(DEFINED ENV{TZDIR})
    add_definitions(-DTZDIR="$ENV{TZDIR}")
endif()

ExternalProject_Add(${TZCODE_PROJECT}
    EXCLUDE_FROM_ALL 1
    DOWNLOAD_DIR ${TZCODE_BINARY_DIR}
    TMP_DIR ${TZCODE_BINARY_DIR}/tmp
    STAMP_DIR ${TZCODE_BINARY_DIR}/stamp
    SOURCE_DIR ${TZCODE_SOURCE_DIR}

    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    TEST_COMMAND ""

    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${CMAKE_C_FLAGS}
        PACKRATDATA=backzone
        make -C <SOURCE_DIR> ${TZDATA_TARGET}
    BUILD_IN_SOURCE TRUE
    BUILD_BYPRODUCTS ${TZDATA_FULL_TARGET}
)

find_program(PERL perl)

add_custom_target(gen-timezones DEPENDS ${TZCODE_PROJECT})
add_custom_command(TARGET gen-timezones
    COMMAND
    ${PERL} ${TZCODE_SRC}/gen-zone-abbrevs.pl
        ${TZCODE_SRC}/zone-abbrevs.txt ${TZDATA_FULL_TARGET}
        ${TZCODE_SRC}/timezones.h ${TZLUA_SRC}/timezones.lua
    COMMENT "Generating timezones.h and timezones.lua"
)

# We excluded build of gen-timezones and tzcode-iana (main.zi) targets
# from ALL dependencies (see EXCLUDE_FROM_ALL=1) if you, by either reason
# need to regenerate timezones.h then invoke target manually:
# ```sh
#   make gen-timezones VERBOSE=1
# ```
# it will regenerate `timezones.h`, `timezones.lua` in-place, in their
# repository location.
