## feature/core

* Introduced the new built-in system event `box.wal_error` that is broadcast
  whenever Tarantool fails to commit a transaction to the write-ahead log
  (gh-9405).
