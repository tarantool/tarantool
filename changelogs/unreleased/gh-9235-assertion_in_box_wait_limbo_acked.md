## bugfix/core

* Fixed a bug when the assertion in `box_wait_limbo_acked` would fail. The
  assertion is that the lsn of the last entry in limbo is always positive after
  `wal_sync`. Before the patch, if the `replication_synchro_quorum` was set too
  high on the replica, it would never be reached. After the timeout was
  triggered, the user received a `TimedOut` error. If the quorum was greater
  than or equal to the number of instances in the replica set, the program
  immediately stopped with a segmentation fault (gh-9235).
