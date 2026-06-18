## feature/config

* Extended the alerts API: `set()`, `add()`, `unset()`, and `clear()` now return a
  `bool` indicating whether a change was made. For example, `set()` returns
  `false` if an alert with the same key and message already exists. Also added
  `alerts_namespace:get(key)` to look up an alert by key.
