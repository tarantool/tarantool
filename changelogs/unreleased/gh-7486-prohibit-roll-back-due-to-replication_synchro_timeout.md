## feature/replication

* A new compat option `compat.replication_synchro_timeout` has been added.
  This option determines whether the `replication.synchro_timeout` option rolls
  back transactions. When set to 'new', transactions are not rolled back due to
  a timeout. In this mode `replication.synchro_timeout` is used to wait
  confirmation in promote/demote and gc-checkpointing. If 'old' is set, the
  behavior is no different from what it was before this patch appeared.
* A new `replication.synchro_queue_max_size` option limits the number of
  transactions in the master synchronous queue.
  `replication.synchro_queue_max_size` is measured in the number of bytes to be
  written (0 means unlimited, which was the default behavior before).
  Currently, this option defaults to 16 megabytes.
  (gh-7486)
