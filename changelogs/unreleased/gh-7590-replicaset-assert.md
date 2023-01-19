## bugfix/replication

* Fixed a bug when a replicaset state machine that tracks the number of
  appliers in different states could become inconsistent during
  reconfiguration (gh-7590).
