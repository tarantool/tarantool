#============================================================================#
# build flags
#============================================================================#

# default flags
if (${CMAKE_BUILD_TYPE} STREQUAL "None")
    set (tntnet_cflags "-std=gnu99")
else()
    set (tntnet_cflags "-std=gnu99 -Wall -Wextra")
    set (tntnet_cflags "${tntnet_cflags} -Wno-sign-compare -Wno-strict-aliasing")
endif()

# Only add -Werror if it's a debug build, done by developers.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (tntnet_cflags "${tntnet_cflags} -Werror")
endif()

#============================================================================#
# Build tnt net project
#============================================================================#

#
# source files
#

set (tntnet_sources
	tnt_iob.c
	tnt_io.c
	tnt_opt.c
	tnt_net.c)

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
project(tntnet)
add_library(tntnet STATIC ${tntnet_sources})
set_target_properties(tntnet PROPERTIES COMPILE_FLAGS "${tntnet_cflags}")
set_target_properties(tntnet PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tntnet PROPERTIES OUTPUT_NAME "tarantoolnet")

#
# Shared library
#
project(tntnet_shared)
add_library(tntnet_shared SHARED ${tntnet_sources})
target_link_libraries(tntnet_shared tnt_shared)
set_target_properties(tntnet_shared PROPERTIES OUTPUT_NAME tntnet)
set_target_properties(tntnet_shared PROPERTIES COMPILE_FLAGS "${tntnet_cflags}")
set_target_properties(tntnet_shared PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tntnet_shared PROPERTIES OUTPUT_NAME "tarantoolnet")

#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

install (TARGETS tntnet ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (TARGETS tntnet_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
