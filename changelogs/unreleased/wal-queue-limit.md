## feature/core

* Introduce the concept of WAL queue and a new configuration option:
  `wal_queue_max_size`, measured in bytes, with 16 Mb default.
  The option helps limit the pace at which replica submits new transactions
  to WAL: the limit is checked every time a transaction from master is
  submitted to replica's WAL, and the space taken by a transaction is
  considered empty once it's successfully written (gh-5536).
