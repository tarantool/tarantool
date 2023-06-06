## feature/core

* Introduced the `conn:watch_once()` net.box connection method to get the value
  currently associated with a notification key on a remote instance without
  subscribing to future changes. The new method is implemented using the
  `IPROTO_WATCH_ONCE` request type (gh-6493).
