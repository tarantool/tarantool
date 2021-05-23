## bugfix/core

* Fixed invalid results produced by `json` module's `encode` function when it
  was used from Lua's garbage collector. For instance, in functions used as
  `ffi.gc()` (gh-6050).
