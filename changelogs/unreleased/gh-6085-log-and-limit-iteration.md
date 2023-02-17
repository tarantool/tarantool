## feature/core

* Introduced the mechanism for catching fibers running without yielding for too
  long. Now box operations, such as `select` and `replace`, will throw an error
  if the fiber execution time since yield exceeds the limit. The limit can also
  be checked from the application code with `fiber.check_slice()`. The default
  limit is controlled by the new `compat` option `fiber_slice_default`. The old
  default is no limit. The new default is one second. You can overwrite it with
  `fiber.set_slice()` (gh-6085).
