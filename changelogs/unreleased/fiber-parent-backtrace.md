## feature/core

* Implemented collection of parent backtrace for the newly created fibers.
  To enable the feature, call `fiber.parent_backtrace_enable`. To disable it,
  call `fiber.parent_backtrace_disable`: disabled by default (gh-4302).
