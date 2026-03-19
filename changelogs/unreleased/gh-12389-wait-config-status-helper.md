## feature/config

* Added `config:wait_status(statuses, [timeout])` to wait until the instance
  reaches one of the specified configuration statuses and returns the actual
  status observed when the wait finishes (gh-12389).
