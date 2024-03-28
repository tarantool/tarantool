## feature/replication

* Added a compat option `box_cfg_replication_synchro_timeout` to control
  whether `replication_synchro_timeout` is accessible from `box.cfg`. It's
  "old" by default, meaning the option is available and has the same default
  value as before (5 seconds). If set to "new", the tarantool will act as if
  this option does not exist. The noticeable difference in new behavior is
  that a synchronous transaction can remain in the synchro queue indefinitely
  until it reaches a quorum of confirmations, just like canonical Raft
  suggests.
  By default, the option is "old" and will be switched to "new" in the next
  major release. If you wish to try out the new behaviour, you have to set the
  option to "new" before the initial `box.cfg{}` call in your application.

* Since with the new behavior synchronous transactions can collect
  confirmations indefinitely, the queue can grow very large. You need to be
  able to limit the size of a synchronous queue. Added configuration option
  `replication_synchro_queue_max_size` that puts a limit on the number of
  transactions in a synchronous queue. `replication_synchro_queue_max_size`
  is measured in number of bytes to be written (0 means unlimited, which was
  the default behaviour before). This affects both the behavior of the master
  and the behavior of the replica, and defaults to 16 megabytes.
(gh-7486)
