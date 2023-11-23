## feature/core

* `box.iproto.override()` was switched to the universal trigger registry. As a
  side effect, now this function does not raise an error when a wrong request
  type is passed. All such errors are logged with `CRITICAL` level (gh-8138).
