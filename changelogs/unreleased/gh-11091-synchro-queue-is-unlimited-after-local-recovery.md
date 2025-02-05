## bugfix/replication

* Fixed a bug when the synchronous queue size limit was not enabled
  after recovering from local snapshot-files, i.e. its size was not limited by
  `replication_synchro_queue_max_size` (gh-11091).
