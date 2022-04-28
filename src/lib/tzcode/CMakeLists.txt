add_library(tzcode STATIC
            strftime.c strptime.c timelocal.c localtime.c timezone.c)
target_link_libraries(tzcode)

set(TZCODE_PROJECT tzcode-iana)
set(TZCODE_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TZCODE_PREFIX ${TZCODE_BUILD_DIR}/tz)
set(TZDATA_TARGET main.zi)

set(TZDATA_FULL_TARGET
    ${TZCODE_PREFIX}/src/${TZCODE_PROJECT}/${TZDATA_TARGET})

ExternalProject_Add(${TZCODE_PROJECT}
    GIT_REPOSITORY https://github.com/tarantool/tz
    GIT_TAG 2022a
    GIT_SHALLOW YES

    INSTALL_DIR ${TZCODE_BUILD_DIR}/install

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
