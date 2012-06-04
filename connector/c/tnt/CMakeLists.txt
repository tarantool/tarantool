#============================================================================#
# build flags
#============================================================================#

# default flags
if (${CMAKE_BUILD_TYPE} STREQUAL "None")
    set (tnt_cflags "-std=gnu99")
else()
    set (tnt_cflags "-std=gnu99 -Wall -Wextra")
    set (tnt_cflags "${tnt_cflags} -Wno-sign-compare -Wno-strict-aliasing")
endif()

# Only add -Werror if it's a debug build, done by developers.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (tnt_cflags "${tnt_cflags} -Werror")
endif()

#============================================================================#
# Build tnt project
#============================================================================#

#
# source files
#
set (tnt_sources
	tnt_mem.c
	tnt_enc.c
	tnt_tuple.c
	tnt_iter.c
	tnt_stream.c
	tnt_buf.c
	tnt_ping.c
	tnt_insert.c
	tnt_update.c
	tnt_delete.c
	tnt_call.c
	tnt_select.c
	tnt_reply.c
	tnt_request.c)

#----------------------------------------------------------------------------#
# Builds
#----------------------------------------------------------------------------#

# Here we manage to build static/dynamic libraries ourselves,
# do not use the top level settings.
string(REPLACE "-static" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

#
# Static library
#
project(tnt)
add_library(tnt STATIC ${tnt_sources})
set_target_properties(tnt PROPERTIES COMPILE_FLAGS "${tnt_cflags}")
set_target_properties(tnt PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tnt PROPERTIES OUTPUT_NAME "tarantool")

#
# Shared library
#
project(tnt_shared)
add_library(tnt_shared SHARED ${tnt_sources})
set_target_properties(tnt_shared PROPERTIES OUTPUT_NAME tnt)
set_target_properties(tnt_shared PROPERTIES COMPILE_FLAGS "${tnt_cflags}")
set_target_properties(tnt_shared PROPERTIES VERSION ${LIBTNT_VERSION} SOVERSION ${LIBTNT_SOVERSION})
set_target_properties(tnt_shared PROPERTIES OUTPUT_NAME "tarantool")

#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

# install static library
install_targets(/lib tnt)
# install shared library
install_targets(/lib tnt_shared)
