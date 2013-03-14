cmake_minimum_required(VERSION 2.8)

find_package(LLVM)
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})


add_llvm_loadable_module( libGNUObjCRuntime
  ClassIMPCache.cpp
  ClassMethodInliner.cpp
  IvarPass.cpp
  ObjectiveCOpts.cpp
  TypeFeedbackDrivenInliner.cpp
  ClassLookupCache.cpp
  IMPCacher.cpp
  LoopIMPCachePass.cpp
  TypeFeedback.cpp
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros")

set(CMAKE_CXX "clang++")

EXEC_PROGRAM(llvm-config
	ARGS --src-root
	OUTPUT_VARIABLE LLVM_SRC)
EXEC_PROGRAM(llvm-config
	ARGS --obj-root
	OUTPUT_VARIABLE LLVM_OBJ)
EXEC_PROGRAM(llvm-config
	ARGS --version
	OUTPUT_VARIABLE LLVM_VER)

string(REGEX REPLACE "([0-9]*).([0-9]*).*" "-DLLVM_MAJOR=\\1 -DLLVM_MINOR=\\2" LLVM_VERSION "${LLVM_VER}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_VERSION} -fno-rtti")
include_directories( ${LLVM_INCLUDE_DIRS} "${LLVM_SRC}/include/" "${LLVM_OBJ}/include/")

