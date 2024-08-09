## feature/replication

* This patch allows the `replication_synchro_timeout` option to be set to 0,
  which means an infinite timeout.
* A new compat option `box_cfg_replication_synchro_timeout` has been added.
  This option is `'old'` by default. `box_cfg_replication_synchro_timeout` can
  only be set to `'new'` if `replication_synchro_timeout` is already set to 0.
  With new behavior, option `replication_synchro_timeout` deprecated (it can
  only take the value 0).
* A new `replication_synchro_queue_max_size` option puts a limit on the number
  of transactions in the master synchronous queue.
  `replication_synchro_queue_max_size` is measured in the number of bytes to be
  written (0 means unlimited, which was the default behavior before). Currently
  this option defaults to 16 megabytes.
  (gh-7486)
