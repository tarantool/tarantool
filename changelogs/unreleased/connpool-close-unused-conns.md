## feature/connpool

* `experimental.connpool` now automatically closes unused connections opened by
  `connpool.call()` and `connpool.filter()` upon the timeout configured using
  the `connpool.set_idle_timeout()` method or the `connpool.idle_timeout`
  configuration option.
