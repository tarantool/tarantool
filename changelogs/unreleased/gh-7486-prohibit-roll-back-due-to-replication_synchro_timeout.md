## feature/replication

This patch is designed to prohibit automatic transaction rollback due to
`replication_synchro_timeout`. To do this, the following was done:
* A new compat option `box_cfg_replication_synchro_timeout` has been added.
  This option determines whether the `replication_synchro_timeout` option rolls
  back transactions. When set to 'new', transactions are not rolled back due to
  a timeout. In this mode `replication_synchro_timeout` is used to wait
  confirmation in promote/demote and gc-checkpointing. If 'old' is set, the
  behavior is no different from what it was before this patch appeared.
* A new `replication_synchro_queue_max_size` option limits the number of
  transactions in the master synchronous queue.
  `replication_synchro_queue_max_size` is measured in the number of bytes to be
  written (0 means unlimited, which was the default behavior before).
  Currently, this option defaults to 16 megabytes.
  (gh-7486)
