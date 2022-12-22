## bugfix/replication

* Fixed a bug when a replicaset state machine, tracking the number of
  appliers according to their states, could become inconsistent during
  reconfiguration (gh-7590).
