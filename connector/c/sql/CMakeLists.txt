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
# Build tnt projects
#============================================================================#

#
# source files
#

set (tntsql_sources tnt_utf8.c tnt_lex.c tnt_sql.c)

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

project(tntsql)
add_library(tntsql_shared SHARED ${tntsql_sources})
set_target_properties(tntsql_shared PROPERTIES OUTPUT_NAME tntsql)
set_target_properties(tntsql_shared PROPERTIES COMPILE_FLAGS "${tntsql_cflags}")


#----------------------------------------------------------------------------#
# Install
#----------------------------------------------------------------------------#

# headers
install(FILES tnt_sql.h DESTINATION include/libtnt)

# install static library
install_targets(/lib tntsql)
# install shared library
install_targets(/lib tntsql_shared)
