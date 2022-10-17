## feature/core/fiber

* Fiber channel now has a compatibility option in tarantool.compat, that
  provides transition from force channel `close()` to graceful `close()`, i.e.
  closing channel for writing while existing messages can still be extracted
  (gh-7746).
