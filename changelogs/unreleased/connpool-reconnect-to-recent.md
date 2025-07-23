## feature/connpool

* The `experimental.connpool` now tries to reconnect to recently accessed instances
  when they become unavailable. Reconnect attempts happen after a constant
  interval and are stopped if the instance is no longer needed. The interval is
  controllable by a `connpool.reconnect_after` configuration option.
