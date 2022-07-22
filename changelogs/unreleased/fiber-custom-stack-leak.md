## bugfix/core

* Fixed a bug that was making all fibers created with `fiber_attr_setstacksize()`
  leak until the thread exit. Their stacks also leaked except when
  `fiber_set_joinable(..., true)` was used.
