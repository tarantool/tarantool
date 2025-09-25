## bugfix/replication

* Fixed a bug where `box.begin{txn_isolation = 'linearizable'}` could crash when
  the max size of the synchronous transactions queue was reached (the setting
  `box.cfg.replication_synchro_queue_max_size`) (gh-11807).
