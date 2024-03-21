## feature/replication

* Added a compat option `box_cfg_replication_synchro_timeout` to control
  whether `replication_synchro_timeout` is accessible from `box.cfg`. It's
  `old` by default, meaning the option is available and has the same default
  value as before (5 seconds). If set to `new`, Tarantool will act as if
  this option does not exist. The noticeable difference in new behavior is
  that a synchronous transaction can remain in the synchro queue indefinitely
  until it reaches a quorum of confirmations, just like canonical Raft
  suggests.
  By default, the option is `old` and will be switched to `new` in the next
  major release. If you wish to try out the new behavior, you need to set the
  option to `new`.
* Since with the new behavior of synchronous transactions can collect
  confirmations indefinitely, the queue can grow very large. You need to be
  able to limit the size of a synchronous queue.
  A new `replication_synchro_queue_max_size` option puts a limit on the number of
  transactions in the master synchronous queue. `replication_synchro_queue_max_size`
  is measured in the number of bytes to be written (0 means unlimited, which was the
  default behavior before). This affects only the behavior of a master, and defaults
  to 16 megabytes.
  (gh-7486)
