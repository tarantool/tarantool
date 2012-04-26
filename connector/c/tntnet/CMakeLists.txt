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
string(REPLACE "-static" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

#
# Static library
#

project(tntnet)
add_library(tntnet STATIC ${tntnet_sources})
set_target_properties(tntnet PROPERTIES COMPILE_FLAGS "${tntnet_cflags}")

#
# Shared library
#

project(tntnet_shared)
add_library(tntnet_shared SHARED ${tntnet_sources})
target_link_libraries(tntnet_shared tnt_shared)
set_target_properties(tntnet_shared PROPERTIES OUTPUT_NAME tntnet)
set_target_properties(tntnet_shared PROPERTIES COMPILE_FLAGS "${tntnet_cflags}")

#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

# install static library
install_targets(/lib tntnet)
# install shared library
install_targets(/lib tntnet_shared)
