## feature/fiber

* Exported `fiber_set_name_n`, `fiber_name`, `fiber_id`, `fiber_csw` and
  `fiber_find` into the public C API and usable via FFI as well.
* Make `fiber_set_joinable`, `fiber_set_ctx` and `fiber_get_ctx`
  treat the NULL argument as the current fiber.
