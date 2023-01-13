## feature/core

* **[Breaking change]** Introduced the mechanism for catching fibers running
  without yielding for too long. Now box operations, such as `select` and
  `replace`, will throw an error if the fiber execution time since yield
  exceeds the limit. The limit can also be checked from the application code
  with `fiber.check_slice()`. The default limit is one second; you can
  overwrite it with `fiber.set_slice()` (gh-6085).

----

Breaking change: long-running non-yielding fibers can be interrupted because of
the fiber execution time slice limit.
