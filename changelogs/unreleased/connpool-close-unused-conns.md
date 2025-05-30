## feature/connpool

* `experimental.connpool` now automatically closes unused connections opened by
  `connpool.call()` and `connpool.filter()`. This deadline can be configured
  using the `connpool.set_idle_timeout()` method or the `connpool.idle_timeout`
  configuration option.
