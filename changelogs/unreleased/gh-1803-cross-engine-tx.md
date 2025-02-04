## feature/box

* Added support for cross-engine transactions. It is now possible to mix
  statements for different storage engines in the same transaction. Note
  that to mix memtx and vinyl statements, the configuration option
  `database.memtx_use_mvcc_engine` must be enabled (gh-1803).
