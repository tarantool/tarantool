## bugfix/core

* Fixed a bug when the assertion in `box_wait_limbo_acked` would fail. The
  assertion is that the lsn of the last entry in limbo is always positive after
  `wal_sync`. What happened in the release build before the patch? If the
  `replication_synchro_quorum` is set too high on the replica, then it will never
  be reached. After the timeout is triggered, the user will receive a `TimedOut`
  error. If `replication_synchro_quorum` <= number of instances in the replica
  set, the program will immediately stop with a `Segmentation fault` (gh-9235).
