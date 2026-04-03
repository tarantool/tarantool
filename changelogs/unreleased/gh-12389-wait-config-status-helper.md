## feature/config

* Added `config:wish_status(statuses, [timeout])` to wait until the instance
  reaches one of the specified config statuses and return the actual status
  observed when the wait finishes (gh-12389).
