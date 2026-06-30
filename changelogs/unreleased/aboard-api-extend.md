## feature/config

* Extended the alerts API: `set()`, `add()`, `unset()`, `clear()` now return
  `bool` indicating whether a change was made. For instance, `set()` returns
  `false` if an alert with the same key and message already exists. Added
  `alerts_namespace:get(key)` to look up an alert by key.
