#============================================================================#
# build flags
#============================================================================#

# default flags
if (${CMAKE_BUILD_TYPE} STREQUAL "None")
    set (tntrpl_cflags "-std=gnu99")
else()
    set (tntrpl_cflags "-std=gnu99 -Wall -Wextra")
    set (tntrpl_cflags "${tntrpl_cflags} -Wno-sign-compare -Wno-strict-aliasing")
endif()

# Only add -Werror if it's a debug build, done by developers.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (tntrpl_cflags "${tntrpl_cflags} -Werror")
endif()

#============================================================================#
# Build tnt rpl project
#============================================================================#

#
# source files
#

set (tntrpl_sources tnt_log.c tnt_dir.c tnt_xlog.c tnt_snapshot.c tnt_rpl.c
     ${CMAKE_SOURCE_DIR}/third_party/crc32.c)

#----------------------------------------------------------------------------#
# Builds
#----------------------------------------------------------------------------#

# Here we manage to build static/dynamic libraries ourselves,
# do not use the top level settings.
string(REPLACE "-static-libgcc" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
string(REPLACE "-static" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

if (CMAKE_COMPILER_IS_GNUCC AND NOT CMAKE_COMPILER_IS_CLANG)
    set (tnt_cflags "${tnt_cflags} -static-libgcc")
endif()

#
# Static library
#

project(tntrpl)
add_library(tntrpl STATIC ${tntrpl_sources})
set_target_properties(tntrpl PROPERTIES COMPILE_FLAGS "${tntrpl_cflags}")
set_target_properties(tntrpl PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tntrpl PROPERTIES OUTPUT_NAME "tarantoolrpl")

#
# Shared library
#

project(tntrpl_shared)
add_library(tntrpl_shared SHARED ${tntrpl_sources})
target_link_libraries(tntrpl_shared tnt_shared tntnet_shared)
set_target_properties(tntrpl_shared PROPERTIES OUTPUT_NAME tntrpl)
set_target_properties(tntrpl_shared PROPERTIES COMPILE_FLAGS "${tntrpl_cflags}")
set_target_properties(tntrpl_shared PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tntrpl_shared PROPERTIES OUTPUT_NAME "tarantoolrpl")

#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

install (TARGETS tntrpl ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (TARGETS tntrpl_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
