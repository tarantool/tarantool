## bugfix/core

* Extensive usage of `uri` and `uuid` modules with debug log level could lead to
  a crash or corrupted result of the functions from these modules. Also their
  usage from the callbacks passed to `ffi.gc()` could lead to the same but much
  easier. The same could happen with some functions from the modules `fio`,
  `box.tuple`, `iconv` (gh-5632).
