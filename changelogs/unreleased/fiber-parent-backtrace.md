## feature/core

* Implemented collection of parent backtrace for newly created fibers.
  To enable the feature, call `fiber.parent_backtrace_enable` — to disable it
  call `fiber.parent_backtrace_disable`: disabled by default (gh-4302).
