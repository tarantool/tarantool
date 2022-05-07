add_library(tzcode STATIC
            strftime.c strptime.c timelocal.c localtime.c timezone.c)
target_link_libraries(tzcode)

set(TZCODE_PROJECT tzcode-iana)
set(TZCODE_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TZCODE_PREFIX_DIR tz)
set(TZCODE_PREFIX ${TZCODE_BUILD_DIR}/${TZCODE_PREFIX_DIR})
set(TZDATA_TARGET main.zi)
set(TZCODE_SRC ${PROJECT_SOURCE_DIR}/src/lib/tzcode)

set(TZDATA_FULL_TARGET
    ${TZCODE_PREFIX}/src/${TZCODE_PROJECT}/${TZDATA_TARGET})

ExternalProject_Add(${TZCODE_PROJECT}
    GIT_REPOSITORY https://github.com/tarantool/tz
    GIT_TAG 2022a
    GIT_SHALLOW YES

    EXCLUDE_FROM_ALL 1
    INSTALL_DIR ${TZCODE_BUILD_DIR}/install
    PREFIX ${TZCODE_PREFIX_DIR}

    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""

    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${CMAKE_C_FLAGS}
        PACKRATDATA=backzone
        make -C <SOURCE_DIR> ${TZDATA_TARGET}
    BUILD_IN_SOURCE TRUE
    TEST_COMMAND ""
    INSTALL_COMMAND make DESTDIR=<INSTALL_DIR> install
    BUILD_BYPRODUCTS ${TZDATA_FULL_TARGET}
)

find_program(PERL perl)

add_custom_target(gen-timezones DEPENDS ${TZCODE_PROJECT})
add_custom_command(TARGET gen-timezones
    COMMAND
    ${PERL} ${TZCODE_SRC}/gen-zone-abbrevs.pl ${TZCODE_SRC}/zone-abbrevs.txt
    ${TZDATA_FULL_TARGET} > ${TZCODE_SRC}/timezones.h
    COMMENT "Generating timezones.h"
)

# We excluded build of gen-timezones and tzcode-iana (main.zi) targets
# from ALL dependencies (see EXCLUDE_FROM_ALL=1) if you, by either reason
# need to regenerate timezones.h then invoke target manually:
# ```sh
#   make gen-timezones VERBOSE=1
# ```
# it will regenerate `timezones.h` in-place, in their repository location.
