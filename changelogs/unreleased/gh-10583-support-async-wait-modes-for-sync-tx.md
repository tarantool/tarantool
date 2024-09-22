## feature/box

* Added support for asynchronous wait modes (`box.commit{wait = ...}`) to
  synchronous transactions. Changes committed this way can be observed with the
  `read-committed` isolation level. Such transactions will not get
  rolled back due to `replication_synchro_timeout` (gh-10583).
