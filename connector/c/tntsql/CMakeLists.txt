#============================================================================#
# build flags
#============================================================================#

# default flags
if (${CMAKE_BUILD_TYPE} STREQUAL "None")
    set (tntsql_cflags "-std=gnu99")
else()
    set (tntsql_cflags "-std=gnu99 -Wall -Wextra")
    set (tntsql_cflags "${tntsql_cflags} -Wno-sign-compare -Wno-strict-aliasing")
endif()

# Only add -Werror if it's a debug build, done by developers.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (tntsql_cflags "${tntsql_cflags} -Werror")
endif()

#============================================================================#
# Build tnt sql project
#============================================================================#

#
# source files
#

set (tntsql_sources
	tnt_utf8.c
	tnt_lex.c
	tnt_sql.c)

#----------------------------------------------------------------------------#
# Builds
#----------------------------------------------------------------------------#

# Here we manage to build static/dynamic libraries ourselves,
# do not use the top level settings.
string(REPLACE "-static" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

#
# Static library
#

project(tntsql)
add_library(tntsql STATIC ${tntsql_sources})
set_target_properties(tntsql PROPERTIES COMPILE_FLAGS "${tntsql_cflags}")

#
# Shared library
#

project(tntsql_shared)
add_library(tntsql_shared SHARED ${tntsql_sources})
target_link_libraries(tntsql_shared tnt_shared)
set_target_properties(tntsql_shared PROPERTIES OUTPUT_NAME tntsql)
set_target_properties(tntsql_shared PROPERTIES COMPILE_FLAGS "${tntsql_cflags}")

#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

# install static library
install_targets(/lib tntsql)
# install shared library
install_targets(/lib tntsql_shared)
