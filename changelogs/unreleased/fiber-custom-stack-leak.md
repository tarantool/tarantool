## bugfix/core

* Fixed a bug due to which all fibers created with `fiber_attr_setstacksize()`
  leaked until the thread exit. Their stacks also leaked except when
  `fiber_set_joinable(..., true)` was used.
