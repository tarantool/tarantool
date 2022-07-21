## bugfix/core

* Removed an assertion on `fiber_wakeup()` calls with dead fibers
  in debug builds. Such behavior was inconsistent with release builds,
  in which the same calls were allowed (gh-5843).
