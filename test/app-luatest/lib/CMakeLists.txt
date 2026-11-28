build_module(libyield libyield.c)
build_module(tx_user_fiber_pool tx_user_fiber_pool.cc)
target_link_libraries(tx_user_fiber_pool msgpuck)

# The module uses the Linux-specific gettid syscall.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  build_module(gh_12477_signal_logging gh_12477_signal_logging.c)
endif()
