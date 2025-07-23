## feature/connpool

* The `experimental.connpool` methods now try to reconnect to recently accessed
  instances when they become unavailable. Reconnect attempts happen after a
  constant interval and are stopped if the instance is no longer needed.
