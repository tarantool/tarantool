## bugfix/core

* Fixed lack of testing for non noinable fibers in `fiber_join()` call.
  This could lead to unpredictable results. Note the issue affects C
  level only, in Lua interface `fiber:join()` the protection is
  turned on already.
