set(LPM_TARGET external.protobuf_mutator)
set(LPM_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/${LPM_TARGET})
set(LPM_REPO https://github.com/google/libprotobuf-mutator.git)

set(LPM_INCLUDE_DIRS ${LPM_INSTALL_DIR}/include)
include_directories(${LPM_INCLUDE_DIRS})
include_directories(${LPM_INCLUDE_DIRS}/libprotobuf-mutator)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# This exact order due to dependencies of libraries.
set(LPM_LIBRARIES protobuf-mutator-libfuzzer protobuf-mutator)

foreach(lib ${LPM_LIBRARIES})
  set(LIB_PATH ${LPM_INSTALL_DIR}/lib/lib${lib}.a)
  list(APPEND LPM_BUILD_BYPRODUCTS ${LIB_PATH})
  add_library(${lib} STATIC IMPORTED)
  set_property(TARGET ${lib} PROPERTY IMPORTED_LOCATION
               ${LIB_PATH})
  add_dependencies(${lib} ${LPM_TARGET})
endforeach(lib)

# Part of protobuf.cmake moved here due to build problems of
# libprotobuf-mutator with separate protobuf installation

# FindProtobuf is present in newer versions of CMake:
# https://cmake.org/cmake/help/latest/module/FindProtobuf.html
# We only need protobuf_generate_cpp from FindProtobuf, and the rest will be
# downloaded with LPM.
include (FindProtobuf)

set(PROTOBUF_TARGET external.protobuf)
set(PROTOBUF_INSTALL_DIR ${LPM_INSTALL_DIR}/src/${LPM_TARGET}-build/${PROTOBUF_TARGET})

set(PROTOBUF_INCLUDE_DIRS ${PROTOBUF_INSTALL_DIR}/include)
include_directories(${PROTOBUF_INCLUDE_DIRS})

set(PROTOBUF_LIBRARIES protobuf)
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  set(PROTOBUF_LIBRARIES protobufd)
endif(CMAKE_BUILD_TYPE)

foreach(lib ${PROTOBUF_LIBRARIES})
  set(LIB_PROTOBUF_PATH ${PROTOBUF_INSTALL_DIR}/lib/lib${lib}.a)
  list(APPEND PROTOBUF_BUILD_BYPRODUCTS ${LIB_PROTOBUF_PATH})

  add_library(${lib} STATIC IMPORTED)
  set_property(TARGET ${lib} PROPERTY IMPORTED_LOCATION
               ${LIB_PROTOBUF_PATH})
  add_dependencies(${lib} ${PROTOBUF_TARGET} ${LPM_TARGET})
endforeach(lib)

set(PROTOBUF_PROTOC_EXECUTABLE ${PROTOBUF_INSTALL_DIR}/bin/protoc)
list(APPEND PROTOBUF_BUILD_BYPRODUCTS ${PROTOBUF_PROTOC_EXECUTABLE})

if(${CMAKE_VERSION} VERSION_LESS "3.10.0")
  set(PROTOBUF_PROTOC_TARGET protoc)
else()
  set(PROTOBUF_PROTOC_TARGET protobuf::protoc)
endif()

if(NOT TARGET ${PROTOBUF_PROTOC_TARGET})
  add_executable(${PROTOBUF_PROTOC_TARGET} IMPORTED)
endif()
set_property(TARGET ${PROTOBUF_PROTOC_TARGET} PROPERTY IMPORTED_LOCATION
             ${PROTOBUF_PROTOC_EXECUTABLE})
add_dependencies(${PROTOBUF_PROTOC_TARGET} ${PROTOBUF_TARGET} ${LPM_TARGET})

include (ExternalProject)
ExternalProject_Add(${LPM_TARGET}
  PREFIX ${LPM_TARGET}
  GIT_REPOSITORY ${LPM_REPO}
  GIT_TAG a304ec4
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND} ${LPM_INSTALL_DIR}/src/${LPM_TARGET}
        -G${CMAKE_GENERATOR}
        -DCMAKE_INSTALL_PREFIX=${LPM_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
        -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DLIB_PROTO_MUTATOR_DOWNLOAD_PROTOBUF=ON
        -DLIB_PROTO_MUTATOR_TESTING=OFF
        -DLIB_PROTO_MUTATOR_WITH_ASAN=${ENABLE_ASAN}
  BUILD_BYPRODUCTS ${LPM_BUILD_BYPRODUCTS} ${PROTOBUF_BUILD_BYPRODUCTS}
)

# CMake 3.6+: All input and output variables use the Protobuf_ prefix.
# Variables with PROTOBUF_ prefix are still supported for compatibility.
# See https://cmake.org/cmake/help/latest/module/FindProtobuf.html
set(Protobuf_INCLUDE_DIRS ${PROTOBUF_INCLUDE_DIRS})
set(Protobuf_LIBRARIES ${PROTOBUF_LIBRARIES})
set(Protobuf_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
