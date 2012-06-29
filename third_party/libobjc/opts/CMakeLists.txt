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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros -DLLVM_MAJOR=3 -DLLVM_MINOR=0")

add_llvm_library_dependencies( libGNUObjCRuntime
  LLVMAnalysis
  LLVMCore
  LLVMSupport
  LLVMTarget
  LLVMipa
  LLVMipo
)
